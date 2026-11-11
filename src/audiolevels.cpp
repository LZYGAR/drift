#include "audiolevels.h"

#include "effects.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <sys/prctl.h>

namespace {

constexpr double kLowestHz = 40.0;
constexpr double kHighestHz = 16000.0;
constexpr double kBandQ = 3.8;
// What a band's energy has to reach to count as "full" and as "nothing", in dB.
constexpr double kFloorDb = -66.0;
constexpr double kCeilingDb = -8.0;
// A quiet room never counts as music: the scale never zooms in past this.
constexpr double kMinPeak = 0.45;
// The loudest-recent reference forgets about 9% of itself a second.
constexpr double kPeakFade = 0.9985;
// After a loud moment the values fall over a few frames; the smoothing seen on screen is done later.
constexpr double kRelease = 0.3;
constexpr int kKickLowBands = 4;   // 40 to 88 Hz
constexpr int kMidFirst = 4;       // 115 Hz up to 1.5 kHz
constexpr int kTrebleFirst = 15;   // 1.9 kHz and up
constexpr int kSnareFirst = 8, kSnareLast = 16; // 330 Hz to 2.6 kHz
constexpr int kHatFirst = 18;      // 4.4 kHz and up

double clamp01(double v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

double smoothstep(double x, double low, double high) {
    const double t = clamp01((x - low) / (high - low));
    return t * t * (3 - 2 * t);
}

double chase(double value, double target) {
    return target > value ? target : value + (target - value) * kRelease;
}

QString syncKey(const QString &sinkName) {
    return QStringLiteral("sync/") + QString(sinkName).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")), QStringLiteral("_"));
}

} // namespace

double LevelAnalyzer::bandCentre(int band) {
    return kLowestHz * std::pow(kHighestHz / kLowestHz, double(band) / (kBands - 1));
}

LevelAnalyzer::LevelAnalyzer() {
    for (int i = 0; i < kBands; ++i) {
        // A band-pass filter per band (RBJ cookbook, constant 0 dB peak gain).
        const double w0 = 2.0 * M_PI * bandCentre(i) / kRate;
        const double alpha = std::sin(w0) / (2.0 * kBandQ);
        const double a0 = 1.0 + alpha;
        m_b0[i] = alpha / a0;
        m_b2[i] = -alpha / a0;
        m_a1[i] = -2.0 * std::cos(w0) / a0;
        m_a2[i] = (1.0 - alpha) / a0;
    }
    // The kick's low-pass: 130 Hz, Butterworth.
    const double w0 = 2.0 * M_PI * 130.0 / kRate;
    const double alpha = std::sin(w0) / (2.0 * 0.7071);
    const double a0 = 1.0 + alpha;
    m_lpB0 = (1.0 - std::cos(w0)) / 2.0 / a0;
    m_lpB1 = (1.0 - std::cos(w0)) / a0;
    m_lpA1 = -2.0 * std::cos(w0) / a0;
    m_lpA2 = (1.0 - alpha) / a0;
    // Quick to rise (2 ms), slower to fall (60 ms): the shape of a kick drum.
    m_envAttack = 1.0 - std::exp(-1.0 / (0.002 * kRate));
    m_envRelease = 1.0 - std::exp(-1.0 / (0.060 * kRate));
}

void LevelAnalyzer::reset() {
    std::fill(std::begin(m_z1), std::end(m_z1), 0.0);
    std::fill(std::begin(m_z2), std::end(m_z2), 0.0);
    std::fill(std::begin(m_sumSquares), std::end(m_sumSquares), 0.0);
    m_totalSquares = 0;
    m_inFrame = 0;
    m_bandPeak = m_levelPeak = kMinPeak;
    m_lpZ[0][0] = m_lpZ[0][1] = m_lpZ[1][0] = m_lpZ[1][1] = 0;
    m_env = m_envAtHop = 0;
    m_kick = m_snare = m_hat = Onset();
    m_previousShaped = {};
    m_frame = Frame();
}

double LevelAnalyzer::decibels(double rms) {
    return 20.0 * std::log10(rms + 1e-9);
}

// Whether `flux` (how fast something is rising) is an onset, judged against the last second.
double LevelAnalyzer::Onset::step(double flux, double minPeak, int cooldownFrames) {
    peak = std::max(peak * 0.998, flux);
    const double f = flux / std::max(peak, minPeak);
    double mean = 0;
    for (double h : history)
        mean += h;
    mean /= history.size();
    double variance = 0;
    for (double h : history)
        variance += (h - mean) * (h - mean);
    const double deviation = std::sqrt(variance / history.size());
    // Well above what has been normal lately, and at least a clear jump in itself.
    const double threshold = std::max(0.28, mean + 1.5 * deviation);
    const bool fire = cooldown == 0 && f > threshold && f >= previous && flux > minPeak * 0.5;
    history[next] = f;
    next = (next + 1) % int(history.size());
    previous = f;
    if (cooldown > 0)
        --cooldown;
    if (!fire)
        return 0;
    cooldown = cooldownFrames;
    return std::min(1.0, 0.5 + (f - threshold));
}

int LevelAnalyzer::feed(const qint16 *samples, int count, std::vector<Frame> *out) {
    int frames = 0;
    for (int n = 0; n < count; ++n) {
        const double x = samples[n] / 32768.0;
        m_totalSquares += x * x;
        for (int i = 0; i < kBands; ++i) {
            const double y = m_b0[i] * x + m_z1[i];
            m_z1[i] = -m_a1[i] * y + m_z2[i];
            m_z2[i] = m_b2[i] * x - m_a2[i] * y;
            m_sumSquares[i] += y * y;
        }
        // The kick's low-passed signal, and its envelope.
        const double y1 = m_lpB0 * x + m_lpZ[0][0];
        m_lpZ[0][0] = m_lpB1 * x - m_lpA1 * y1 + m_lpZ[0][1];
        m_lpZ[0][1] = m_lpB0 * x - m_lpA2 * y1;
        const double y2 = m_lpB0 * y1 + m_lpZ[1][0];
        m_lpZ[1][0] = m_lpB1 * y1 - m_lpA1 * y2 + m_lpZ[1][1];
        m_lpZ[1][1] = m_lpB0 * y1 - m_lpA2 * y2;
        const double magnitude = std::abs(y2);
        m_env += (magnitude - m_env) * (magnitude > m_env ? m_envAttack : m_envRelease);
        if (++m_inFrame == kHop) {
            finishFrame();
            ++frames;
            if (out)
                out->push_back(m_frame);
        }
    }
    return frames;
}

void LevelAnalyzer::finishFrame() {
    double heard[kBands];
    double loudest = 0;
    for (int i = 0; i < kBands; ++i) {
        const double rms = std::sqrt(m_sumSquares[i] / kHop);
        heard[i] = clamp01((decibels(rms) - kFloorDb) / (kCeilingDb - kFloorDb));
        loudest = std::max(loudest, heard[i]);
        m_sumSquares[i] = 0;
    }
    const double total = clamp01((decibels(std::sqrt(m_totalSquares / kHop)) - kFloorDb) / (kCeilingDb - kFloorDb));
    m_totalSquares = 0;
    m_inFrame = 0;

    // Scale against the loudest recent sound: it rises at once and fades slowly.
    m_bandPeak = std::max({m_bandPeak * kPeakFade, loudest, kMinPeak});
    m_levelPeak = std::max({m_levelPeak * kPeakFade, total, kMinPeak});

    double shaped[kBands];
    for (int i = 0; i < kBands; ++i)
        shaped[i] = clamp01(std::pow(heard[i] / m_bandPeak, 2.4));
    const double level = clamp01(std::pow(total / m_levelPeak, 1.8));

    double bass = 0, mid = 0, treble = 0;
    for (int i = 0; i < kKickLowBands; ++i)
        bass = std::max(bass, shaped[i]);
    for (int i = kMidFirst; i < kTrebleFirst; ++i)
        mid += shaped[i];
    for (int i = kTrebleFirst; i < kBands; ++i)
        treble += shaped[i];
    mid = clamp01(mid / (kTrebleFirst - kMidFirst) * 1.25);
    treble = clamp01(treble / (kBands - kTrebleFirst) * 1.35);

    // Onsets: the kick from the rise of the low-passed envelope across this frame, the snare and the
    // hat from how much their bands rose since the last frame.
    const double kickFlux = std::max(0.0, m_env - m_envAtHop);
    m_envAtHop = m_env;
    double snareFlux = 0, hatFlux = 0;
    for (int i = kSnareFirst; i <= kSnareLast; ++i)
        snareFlux += std::max(0.0, shaped[i] - m_previousShaped[i]);
    for (int i = kHatFirst; i < kBands; ++i)
        hatFlux += std::max(0.0, shaped[i] - m_previousShaped[i]);
    snareFlux /= (kSnareLast - kSnareFirst + 1);
    hatFlux /= (kBands - kHatFirst);
    for (int i = 0; i < kBands; ++i)
        m_previousShaped[i] = shaped[i];

    // A kick has to have real bass in the spectrum, not just the leak of some other drum's click.
    const double kick = m_kick.step(kickFlux, 0.008, 8);
    m_frame.kick = bass >= 0.3 ? kick : 0.0;
    m_frame.snare = m_snare.step(snareFlux, 0.08, 7);
    m_frame.hat = m_hat.step(hatFlux, 0.08, 4);
    m_frame.level = chase(m_frame.level, level);
    m_frame.bass = chase(m_frame.bass, bass);
    m_frame.mid = chase(m_frame.mid, mid);
    m_frame.treble = chase(m_frame.treble, treble);
    for (int i = 0; i < kBands; ++i)
        m_frame.bands[i] = chase(m_frame.bands[i], shaped[i]);
}

// ---------------------------------------------------------------------------

AudioLevels::AudioLevels(bool synthetic, QObject *parent) : QObject(parent), m_synthetic(synthetic) {
    m_scan.setInterval(400); // quick to find the stream when a song starts; see scan()
    connect(&m_scan, &QTimer::timeout, this, &AudioLevels::scan);
    m_watch.setInterval(400);
    connect(&m_watch, &QTimer::timeout, this, [this] {
        if (m_active && m_lastFrame.isValid() && m_lastFrame.elapsed() > 700)
            setActive(false);
    });
    m_synth.setInterval(16);
    m_synth.setTimerType(Qt::PreciseTimer);
    connect(&m_synth, &QTimer::timeout, this, &AudioLevels::synthesize);
    // When nothing is listening any more, what is shown settles to zero by itself.
    m_fade.setInterval(16);
    connect(&m_fade, &QTimer::timeout, this, [this] {
        if (!step(nowSeconds()))
            m_fade.stop();
        emit levelsChanged();
    });
    m_clock.start();
}

AudioLevels::~AudioLevels() {
    stopCapture();
}

bool AudioLevels::isBluetooth(const QString &sinkName) {
    return sinkName.contains(QLatin1String("bluez"), Qt::CaseInsensitive) || sinkName.contains(QLatin1String("bluetooth"), Qt::CaseInsensitive);
}

// Bluetooth plays sound about 150 to 250 ms after the computer sends it; wired and built-in outputs
// are near enough in time. This is only the starting point, until the listener moves the slider.
int AudioLevels::defaultSyncMs(const QString &sinkName) {
    return isBluetooth(sinkName) ? 180 : 0;
}

void AudioLevels::setPrefsPath(const QString &path) {
    m_prefsPath = path;
    loadSync();
}

void AudioLevels::loadSync() {
    int ms = defaultSyncMs(m_sinkName);
    m_syncChosen = false;
    if (!m_prefsPath.isEmpty() && !m_sinkName.isEmpty()) {
        QSettings prefs(m_prefsPath, QSettings::IniFormat);
        if (prefs.contains(syncKey(m_sinkName))) {
            ms = qBound(0, prefs.value(syncKey(m_sinkName)).toInt(), 800);
            m_syncChosen = true;
        }
    }
    m_syncMs = ms;
    emit syncChanged();
}

void AudioLevels::setSyncMs(int milliseconds) {
    milliseconds = qBound(0, milliseconds, 800);
    if (milliseconds == m_syncMs && m_syncChosen)
        return;
    m_syncMs = milliseconds;
    m_syncChosen = true;
    if (!m_prefsPath.isEmpty() && !m_sinkName.isEmpty()) {
        QSettings prefs(m_prefsPath, QSettings::IniFormat);
        prefs.setValue(syncKey(m_sinkName), milliseconds);
    }
    emit syncChanged();
}

void AudioLevels::useSink(const QString &name) {
    m_sinkName = name;
    loadSync();
}

void AudioLevels::setWanted(bool wanted) {
    if (wanted == m_wanted)
        return;
    m_wanted = wanted;
    emit wantedChanged();
    if (wanted) {
        m_fade.stop();
        if (m_synthetic) {
            m_synth.start();
            setActive(true);
        } else if (!m_missing) {
            scan();
            m_scan.start();
            m_watch.start();
        }
    } else {
        m_scan.stop();
        m_watch.stop();
        m_synth.stop();
        stopCapture();
        setActive(false);
        m_queue.clear();
        // Let the last levels settle to zero instead of freezing on screen.
        m_fade.start();
    }
}

void AudioLevels::setActive(bool active) {
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
}

// Brings what is shown up to `now`: frames that have waited out the delay become the target, and
// everything follows its target with the time that really passed (so it is smooth at any frame rate).
// Returns false once there is nothing left to show.
bool AudioLevels::step(double now) {
    const double dt = m_lastAdvance > 0 ? qBound(0.004, now - m_lastAdvance, 0.05) : 1.0 / 60;
    m_lastAdvance = now;
    double kick = 0, snare = 0, hat = 0;
    while (!m_queue.isEmpty() && m_queue.first().due <= now) {
        m_target = m_queue.first().frame;
        m_queue.removeFirst();
        if (!m_averagesStarted) {
            // The first sound heard sets what is "usual", so the start of a song is not mistaken for a big rise.
            m_bassAvg = m_target.bass;
            m_midAvg = m_target.mid;
            m_trebleAvg = m_target.treble;
            m_averagesStarted = true;
        }
        kick = std::max(kick, m_target.kick);
        snare = std::max(snare, m_target.snare);
        hat = std::max(hat, m_target.hat);
    }
    // No sound coming in (paused, or the stream went away): everything heads for zero.
    if (!m_active && m_queue.isEmpty())
        m_target = LevelAnalyzer::Frame();

    auto follow = [dt](double &value, double target, double up, double down) {
        const double tau = target > value ? up : down;
        value += (target - value) * (1.0 - std::exp(-dt / tau));
    };
    follow(m_frame.level, m_target.level, 0.015, 0.10);
    follow(m_frame.bass, m_target.bass, 0.012, 0.10);
    follow(m_frame.mid, m_target.mid, 0.015, 0.10);
    follow(m_frame.treble, m_target.treble, 0.015, 0.10);
    double biggest = std::max({m_frame.level, m_frame.bass, m_frame.mid, m_frame.treble});
    for (int i = 0; i < LevelAnalyzer::kBands; ++i) {
        follow(m_frame.bands[i], m_target.bands[i], 0.012, 0.085);
        biggest = std::max(biggest, m_frame.bands[i]);
    }
    // Onsets jump to full at once and fade: the cover's punch and the bar's waves.
    m_frame.kick = std::max(m_frame.kick * std::exp(-dt / 0.14), kick);
    m_frame.snare = std::max(m_frame.snare * std::exp(-dt / 0.10), snare);
    m_frame.hat = std::max(m_frame.hat * std::exp(-dt / 0.06), hat);
    biggest = std::max({biggest, m_frame.kick, m_frame.snare, m_frame.hat});

    // The cover. What it should answer to depends on whether there is any bass to answer to.
    auto average = [dt](double &avg, double value, double tau) { avg += (value - avg) * (1.0 - std::exp(-dt / tau)); };
    average(m_bassAvg, m_frame.bass, 1.4);
    average(m_midAvg, m_frame.mid, 1.0);
    average(m_trebleAvg, m_frame.treble, 1.0);
    const double quiet = 1.0 - smoothstep(m_bassAvg, 0.12, 0.35); // 1 when there has been no bass for a while
    // Strong kicks only (a dead zone for weak ones), and bass well above its own recent level.
    const double hard = clamp01((m_frame.kick - 0.25) / 0.75);
    const double bassAbove = clamp01((m_frame.bass - std::max(0.5, m_bassAvg + 0.2)) / 0.4);
    // Everything that is not bass: snares, hats, and mids or treble rising above their usual.
    // (A small wobble in the mids or treble is not an event: it has to rise clearly above its usual first.)
    const double soft = clamp01(std::max({m_frame.snare, 0.8 * m_frame.hat, clamp01((m_frame.mid - m_midAvg - 0.12) * 4.0),
                                          clamp01((m_frame.treble - m_trebleAvg - 0.12) * 4.0)}));
    const double withBass = 0.85 * hard + 0.4 * bassAbove + 0.12 * soft; // the bass leads, the rest barely counts
    const double withoutBass = std::min(1.0, 2.8 * soft + 0.5 * hard);   // so listen harder to the rest
    const double coverTarget = withBass * (1.0 - quiet) + withoutBass * quiet;
    // Instant attack, a short fall: a hit, not a glow.
    m_cover = std::max(coverTarget, m_cover * std::exp(-dt / 0.12));
    biggest = std::max(biggest, m_cover);
    if (coverTarget < 0.3)
        m_coverHigh = false;
    else if (coverTarget > 0.55 && !m_coverHigh && now - m_lastCoverHit > 0.18) {
        m_coverHigh = true;
        m_lastCoverHit = now;
        emit coverHit(coverTarget);
    }

    if (kick > 0) {
        m_pulses.append({now, kick});
        emit beatShown(kick);
    }
    while (!m_pulses.isEmpty() && now - m_pulses.first().time > 1.5)
        m_pulses.removeFirst();
    if (biggest < 0.003) {
        m_frame = LevelAnalyzer::Frame();
        m_cover = 0;
        m_averagesStarted = false; // the next sound heard starts them afresh
        return m_active || !m_queue.isEmpty();
    }
    return true;
}

void AudioLevels::advance() {
    const bool idle = !m_active && m_queue.isEmpty() && m_cover == 0 && m_frame.level == 0 && m_frame.kick == 0 && m_frame.bass == 0
                      && m_frame.bands[0] == 0 && m_frame.mid == 0 && m_frame.treble == 0 && m_frame.snare == 0 && m_frame.hat == 0;
    if (idle) {
        m_lastAdvance = 0; // the next step starts fresh
        return;
    }
    step(nowSeconds());
    emit levelsChanged();
}

void AudioLevels::injectForTest(const LevelAnalyzer::Frame &frame, double due) {
    m_active = true; // as if a stream were being heard
    m_queue.append({due, frame});
}

QVector<QPair<double, double>> AudioLevels::kickPulses() const {
    QVector<QPair<double, double>> out;
    for (auto it = m_pulses.crbegin(); it != m_pulses.crend(); ++it)
        out.append({m_lastAdvance - it->time, it->strength});
    return out;
}

void AudioLevels::run(const QString &program, const QStringList &args, std::function<void(int, const QByteArray &)> done) {
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit ? code : -1, process->readAllStandardOutput());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        done(-1, {});
        process->deleteLater();
    });
    process->setStandardErrorFile(QProcess::nullDevice());
    process->start(program, args);
}

int AudioLevels::chooseStream(const QByteArray &json, qint64 rootPid,
                              const std::function<qint64(qint64)> &parentOf, int &sinkIndex) {
    const QList<int> owned = Effects::ownedStreams(json, rootPid, -2, parentOf);
    int pick = -1;
    sinkIndex = -1;
    const QJsonArray inputs = QJsonDocument::fromJson(json).array();
    for (const auto &value : inputs) {
        const QJsonObject input = value.toObject();
        const int index = input.value(QStringLiteral("index")).toInt(-1);
        if (!owned.contains(index))
            continue;
        const bool corked = input.value(QStringLiteral("corked")).toBool(false);
        if (pick < 0 || !corked) {
            pick = index;
            sinkIndex = input.value(QStringLiteral("sink")).toInt(-1);
            if (!corked)
                break;
        }
    }
    return pick;
}

QStringList AudioLevels::captureArguments(const QString &sinkName, int streamIndex) {
    return {QStringLiteral("-d"), sinkName + QStringLiteral(".monitor"),
            QStringLiteral("--monitor-stream=%1").arg(streamIndex),
            QStringLiteral("--format=s16le"), QStringLiteral("--rate=%1").arg(LevelAnalyzer::kRate),
            QStringLiteral("--channels=1"), QStringLiteral("--latency-msec=10"),
            QStringLiteral("--client-name=Music Player levels"), QStringLiteral("--stream-name=levels")};
}

// Looks for this app's own playback stream, and listens to it once it is found.
void AudioLevels::scan() {
    if (m_scanning || !m_wanted || m_synthetic || m_missing)
        return;
    // Looking every 0.4 s finds a new song's stream almost at once; once listening, a look every 1.6 s is enough.
    if (m_capture && ++m_scanTick % 4 != 0)
        return;
    m_scanning = true;
    run(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sink-inputs")},
        [this](int code, const QByteArray &out) {
            if (!m_wanted || code != 0) {
                m_scanning = false;
                return;
            }
            int sinkIndex = -1;
            const int stream = chooseStream(out, QCoreApplication::applicationPid(), &Effects::parentPid, sinkIndex);
            if (stream < 0) {
                m_scanning = false;
                stopCapture();
                return;
            }
            // Still the same stream on the same sink: keep listening.
            if (m_capture && m_stream == stream && m_sink == sinkIndex) {
                m_scanning = false;
                return;
            }
            run(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sinks")},
                [this, stream, sinkIndex](int sinksCode, const QByteArray &sinks) {
                    m_scanning = false;
                    if (!m_wanted || sinksCode != 0)
                        return;
                    QString name;
                    for (const auto &value : QJsonDocument::fromJson(sinks).array()) {
                        const QJsonObject sink = value.toObject();
                        if (sink.value(QStringLiteral("index")).toInt(-1) == sinkIndex)
                            name = sink.value(QStringLiteral("name")).toString();
                    }
                    if (!name.isEmpty())
                        startCapture(name, stream, sinkIndex);
                });
        });
}

void AudioLevels::startCapture(const QString &sinkName, int stream, int sinkIndex) {
    stopCapture();
    m_analyzer.reset();
    m_pending.clear();
    m_stream = stream;
    m_sink = sinkIndex;
    // Sound heard through the effects chain ends up on the real output, so the delay is kept under
    // the output's name and the chain's own sink is not taken for a place the sound is heard.
    if (sinkName != m_sinkName && !sinkName.startsWith(QLatin1String("click_fx"))) {
        m_sinkName = sinkName;
        loadSync();
    }
    m_capture = new QProcess(this);
    // If this app dies without cleaning up, the listener must not outlive it.
    m_capture->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    m_capture->setStandardErrorFile(QProcess::nullDevice());
    connect(m_capture, &QProcess::readyReadStandardOutput, this, &AudioLevels::onData);
    connect(m_capture, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            // No parec on this machine: stay quiet and let the interface animate on its own.
            m_missing = true;
            m_scan.stop();
            stopCapture();
        }
    });
    connect(m_capture, &QProcess::finished, this, [this] {
        // The stream went away (a new song, a moved stream). The next scan finds the new one.
        m_stream = -1;
        m_sink = -1;
        if (m_capture) {
            m_capture->deleteLater();
            m_capture = nullptr;
        }
        setActive(false);
    });
    m_capture->start(QStringLiteral("parec"), captureArguments(sinkName, stream));
}

void AudioLevels::stopCapture() {
    m_stream = -1;
    m_sink = -1;
    if (!m_capture)
        return;
    QProcess *process = m_capture;
    m_capture = nullptr;
    process->disconnect(this);
    process->kill();
    process->waitForFinished(300);
    process->deleteLater();
    setActive(false);
}

void AudioLevels::onData() {
    if (!m_capture)
        return;
    m_pending += m_capture->readAllStandardOutput();
    const int samples = m_pending.size() / 2;
    if (samples <= 0)
        return;
    std::vector<LevelAnalyzer::Frame> frames;
    m_analyzer.feed(reinterpret_cast<const qint16 *>(m_pending.constData()), samples, &frames);
    m_pending.remove(0, samples * 2);
    if (frames.empty())
        return;
    // Every frame is kept (an onset is never lost in a batch), each shown after the same delay,
    // spaced as they were heard.
    const double arrival = nowSeconds();
    const double hop = double(LevelAnalyzer::kHop) / LevelAnalyzer::kRate;
    for (size_t k = 0; k < frames.size(); ++k) {
        const double heard = arrival - double(frames.size() - 1 - k) * hop;
        noteHeardKicks(frames[k], heard);
        m_queue.append({heard + m_syncMs / 1000.0, frames[k]});
    }
    m_lastFrame.restart();
    setActive(true);
}

// A steady 120 bpm groove: a kick on every half second with a bass tail, a snare on the off
// beats, hats between, and a spectrum shaped like most music (strong low end, falling away upwards).
void AudioLevels::synthesize() {
    const double t = m_clock.elapsed() / 1000.0;
    const qint64 beatStep = qint64(t / 0.5);
    const bool newBeat = beatStep != m_synthStep;
    m_synthStep = beatStep;
    const double inBeat = std::fmod(t, 0.5);
    const double kickEnv = std::exp(-inBeat * 9.0);
    const bool offBeat = beatStep % 2 == 1;
    const double snareEnv = offBeat ? std::exp(-inBeat * 12.0) : 0.0;
    const double hats = 0.5 + 0.5 * std::sin(t * 31.0) * std::sin(t * 7.3);
    LevelAnalyzer::Frame frame;
    frame.bass = clamp01(0.18 + 0.82 * kickEnv);
    frame.mid = clamp01(0.30 + 0.35 * snareEnv + 0.2 * std::sin(t * 2.1) * std::sin(t * 2.1));
    frame.treble = clamp01(0.18 + 0.5 * hats * (0.4 + 0.6 * snareEnv) + 0.15 * kickEnv);
    frame.level = clamp01(0.35 + 0.5 * kickEnv + 0.15 * snareEnv);
    if (newBeat) {
        frame.kick = 0.9;
        frame.snare = offBeat ? 0.8 : 0.0;
    }
    frame.hat = std::fmod(t, 0.25) < 0.016 ? 0.6 : 0.0;
    for (int i = 0; i < LevelAnalyzer::kBands; ++i) {
        const double position = double(i) / (LevelAnalyzer::kBands - 1);
        const double slope = 1.0 - 0.7 * position;
        const double wobble = 0.5 + 0.5 * std::sin(t * (3.0 + i * 0.5) + i * 1.7);
        const double drive = position < 0.25 ? kickEnv : (position < 0.65 ? 0.5 * kickEnv + 0.5 * snareEnv : hats);
        frame.bands[i] = clamp01(slope * (0.06 + 0.2 * wobble + 0.95 * drive * drive));
    }
    // The made-up groove is delayed like the real thing, so the sync setting can be tried without sound.
    noteHeardKicks(frame, nowSeconds());
    m_queue.append({nowSeconds() + m_syncMs / 1000.0, frame});
    m_lastFrame.restart();
}

// ---- tap to sync ----

void AudioLevels::noteHeardKicks(const LevelAnalyzer::Frame &frame, double heardAt) {
    if (!m_tapping || frame.kick <= 0)
        return;
    m_heardKicks.append(heardAt);
    while (m_heardKicks.size() > 200)
        m_heardKicks.removeFirst();
}

void AudioLevels::startTapSync() {
    m_heardKicks.clear();
    m_taps.clear();
    m_tapping = true;
    m_tapStatus = tr("Tap along with the kick drum you hear");
    emit tapChanged();
}

void AudioLevels::stopTapSync() {
    if (!m_tapping)
        return;
    m_tapping = false;
    m_tapStatus.clear();
    emit tapChanged();
}

void AudioLevels::tap() {
    if (!m_tapping)
        return;
    m_taps.append(nowSeconds());
    if (m_taps.size() < kTapsNeeded) {
        m_tapStatus = tr("Keep tapping… %1 more").arg(kTapsNeeded - m_taps.size());
        emit tapChanged();
        return;
    }
    const int ms = finishTaps();
    m_tapping = false;
    m_tapStatus = ms < 0 ? tr("The taps did not line up with the beat. Try again on a song with a clear kick.")
                         : tr("Set to %1 ms").arg(ms);
    emit tapChanged();
}

int AudioLevels::finishTaps() {
    const int ms = syncFromTaps(m_heardKicks, m_taps);
    if (ms >= 0)
        setSyncMs(ms);
    return ms;
}

int AudioLevels::syncFromTaps(const QVector<double> &kicks, const QVector<double> &taps, int *used) {
    // A tap may land a little early (people anticipate the beat); the speakers can be up to 0.6 s late.
    QVector<double> offsets;
    for (double t : taps) {
        double best = -1;
        for (double k : kicks)
            if (k <= t + 0.04 && k >= t - 0.6)
                best = std::max(best, k);
        if (best >= 0)
            offsets.append(t - best);
    }
    auto median = [](QVector<double> v) {
        std::sort(v.begin(), v.end());
        const int n = int(v.size());
        return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    };
    if (used)
        *used = 0;
    if (offsets.size() < 4)
        return -1;
    const double mid = median(offsets);
    QVector<double> kept;
    for (double o : offsets)
        if (std::abs(o - mid) <= 0.08)
            kept.append(o);
    if (kept.size() < 4 || kept.size() * 3 < offsets.size() * 2) // most taps must agree
        return -1;
    if (used)
        *used = int(kept.size());
    const double ms = std::clamp(median(kept), 0.0, 0.5) * 1000.0;
    return int(std::lround(ms / 5.0) * 5);
}
