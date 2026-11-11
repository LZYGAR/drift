#include "effects.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QtMath>

#include <cmath>
#include <csignal>
#include <sys/prctl.h>

namespace {

constexpr double kMaxShelfDb = 10.0;
constexpr double kMaxMidDb = 8.0;
constexpr double kBassHz = 100.0;
constexpr double kMidHz = 1000.0;
constexpr double kTrebleHz = 8000.0;
constexpr double kShelfQ = 0.7;
constexpr double kMidQ = 0.9;
constexpr double kRate = 48000.0;

// Gain in dB of one biquad at `hz` (Audio EQ Cookbook forms, the same family
// PipeWire's bq_lowshelf, bq_peaking and bq_highshelf use).
enum class Band { LowShelf, Peaking, HighShelf };
double biquadDb(Band band, double centre, double q, double gainDb, double hz) {
    if (qFuzzyIsNull(gainDb))
        return 0;
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2 * M_PI * centre / kRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2 * q);
    double b0, b1, b2, a0, a1, a2;
    if (band == Band::Peaking) {
        b0 = 1 + alpha * a;
        b1 = -2 * cosw;
        b2 = 1 - alpha * a;
        a0 = 1 + alpha / a;
        a1 = -2 * cosw;
        a2 = 1 - alpha / a;
    } else {
        const double root = 2 * std::sqrt(a) * alpha;
        const double sign = band == Band::LowShelf ? 1.0 : -1.0;
        b0 = a * ((a + 1) - sign * (a - 1) * cosw + root);
        b1 = sign * 2 * a * ((a - 1) - sign * (a + 1) * cosw);
        b2 = a * ((a + 1) - sign * (a - 1) * cosw - root);
        a0 = (a + 1) + sign * (a - 1) * cosw + root;
        a1 = -sign * 2 * ((a - 1) + sign * (a + 1) * cosw);
        a2 = (a + 1) + sign * (a - 1) * cosw - root;
    }
    const double w = 2 * M_PI * hz / kRate;
    auto power = [w](double c0, double c1, double c2) {
        const double re = c0 + c1 * std::cos(w) + c2 * std::cos(2 * w);
        const double im = c1 * std::sin(w) + c2 * std::sin(2 * w);
        return re * re + im * im;
    };
    return 10.0 * std::log10(power(b0, b1, b2) / power(a0, a1, a2));
}

constexpr double kBandHz[10] = {31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
// One octave apart, so Q = 1 / (2 sinh(ln2 / 2)) ~ 1.41 makes neighbours overlap into a smooth curve.
constexpr double kBandQ = 1.41;

struct Preset {
    const char *name;
    // Simple mode: bass, mid, treble (-1..1) and reverb (0..1).
    double bass, mid, treble, reverb;
    // Advanced mode: gains in dB at kBandHz.
    double bands[10];
};
const Preset kPresets[] = {
    {"Flat Studio", 0.0, 0.0, 0.0, 0.0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Ultra Bass", 0.9, -0.1, 0.0, 0.0, {10, 9, 6, 3, 0, -1, -1, 0, 0, 0}},
    {"Vocal Pop", -0.1, 0.5, 0.35, 0.0, {-2, -1, 0, 1, 2, 4, 4, 3, 2, 1}},
    {"Late Night Intimate", 0.25, -0.2, -0.4, 0.22, {2, 2, 1, 0, -1, -2, -3, -4, -5, -6}},
    {"Sub-Bass Warmth", 0.7, -0.1, -0.15, 0.05, {7, 6, 3, 0, -1, -1, -1, -2, -2, -2}},
    {"Acoustic Clarity", -0.1, 0.2, 0.45, 0.0, {-2, -1, 0, 0, 1, 2, 3, 4, 4, 3}},
    {"Club Punch", 0.65, -0.15, 0.4, 0.08, {8, 7, 4, 1, -1, -2, 0, 2, 4, 5}},
};

double clampUnit(double value, double low, double high) {
    if (!(value >= low))
        return low;
    return value > high ? high : value;
}

QString num(double value) {
    return QString::number(value, 'f', 3);
}

} // namespace

double Effects::bassDb(double value) {
    return clampUnit(value, -1.0, 1.0) * kMaxShelfDb;
}

double Effects::trebleDb(double value) {
    return clampUnit(value, -1.0, 1.0) * kMaxShelfDb;
}

double Effects::midDb(double value) {
    return clampUnit(value, -1.0, 1.0) * kMaxMidDb;
}

const double *Effects::bandHz() { return kBandHz; }

QVariantList Effects::bandFrequencies() {
    QVariantList list;
    for (double hz : kBandHz)
        list.append(hz);
    return list;
}

double Effects::responseDb(double bass, double mid, double treble, double hz, const QVector<double> &bands) {
    if (bands.size() == kBandCount) {
        double total = 0;
        for (int i = 0; i < kBandCount; ++i)
            total += biquadDb(Band::Peaking, kBandHz[i], kBandQ, clampUnit(bands.at(i), -kMaxBandDb, kMaxBandDb), hz);
        return total;
    }
    return biquadDb(Band::LowShelf, kBassHz, kShelfQ, bassDb(bass), hz)
           + biquadDb(Band::Peaking, kMidHz, kMidQ, midDb(mid), hz)
           + biquadDb(Band::HighShelf, kTrebleHz, kShelfQ, trebleDb(treble), hz);
}

QVariantList Effects::curve(int points) const {
    QVariantList list;
    points = qBound(2, points, 256);
    for (int i = 0; i < points; ++i) {
        const double hz = 20.0 * std::pow(1000.0, double(i) / (points - 1));
        list.append(responseDb(m_bass, m_mid, m_treble, hz, m_advanced ? m_bands : QVector<double>()));
    }
    return list;
}

QStringList Effects::presets() {
    QStringList names;
    for (const auto &preset : kPresets)
        names << QString::fromLatin1(preset.name);
    return names;
}

bool Effects::presetValues(const QString &name, double &bass, double &mid, double &treble, double &reverb) {
    for (const auto &preset : kPresets) {
        if (name.compare(QLatin1String(preset.name), Qt::CaseInsensitive) != 0)
            continue;
        bass = preset.bass;
        mid = preset.mid;
        treble = preset.treble;
        reverb = preset.reverb;
        return true;
    }
    return false;
}

bool Effects::presetBands(const QString &name, QVector<double> &bands) {
    for (const auto &preset : kPresets) {
        if (name.compare(QLatin1String(preset.name), Qt::CaseInsensitive) != 0)
            continue;
        bands = QVector<double>(preset.bands, preset.bands + kBandCount);
        return true;
    }
    return false;
}

// The preset the controls on show match: the ten bands in advanced mode, the three knobs otherwise.
QString Effects::preset() const {
    for (const auto &preset : kPresets) {
        if (qAbs(preset.reverb - m_reverb) >= 0.005)
            continue;
        bool same = true;
        if (m_advanced) {
            for (int i = 0; i < kBandCount && same; ++i)
                same = qAbs(preset.bands[i] - m_bands.at(i)) < 0.05;
        } else {
            same = qAbs(preset.bass - m_bass) < 0.005 && qAbs(preset.mid - m_mid) < 0.005
                   && qAbs(preset.treble - m_treble) < 0.005;
        }
        if (same)
            return QString::fromLatin1(preset.name);
    }
    return {};
}

void Effects::applyPreset(const QString &name) {
    double bass, mid, treble, reverb;
    if (!presetValues(name, bass, mid, treble, reverb))
        return;
    setBass(bass);
    setMid(mid);
    setTreble(treble);
    setReverb(reverb);
    QVector<double> bands;
    if (presetBands(name, bands)) {
        for (int i = 0; i < kBandCount; ++i)
            setBand(i, bands.at(i));
    }
}

QPair<double, double> Effects::mixGains(double reverb) {
    const double wet = clampUnit(reverb, 0.0, 1.0);
    // The dry signal ducks a little as the room grows so the level stays even.
    return {1.0 - 0.35 * wet, 0.9 * wet};
}

QString Effects::filterChainConfig(const QString &impulsePath, double bass, double mid, double treble, double reverb,
                                   const QString &sinkName, const QVector<double> &bands) {
    const auto gains = mixGains(reverb);
    // Advanced mode bypasses the three simple filters (0 dB) and drives the ten peaking bands instead.
    const bool advanced = bands.size() == kBandCount;
    auto bandGain = [&](int i) { return advanced ? clampUnit(bands.at(i), -kMaxBandDb, kMaxBandDb) : 0.0; };
    auto channel = [&](const QString &c) {
        return QStringLiteral(
                   "      { type = builtin name = bass%1 label = bq_lowshelf control = { \"Freq\" = 100.0 \"Q\" = 0.7 \"Gain\" = %2 } }\n"
                   "      { type = builtin name = mid%1 label = bq_peaking control = { \"Freq\" = 1000.0 \"Q\" = 0.9 \"Gain\" = %7 } }\n"
                   "      { type = builtin name = treb%1 label = bq_highshelf control = { \"Freq\" = 8000.0 \"Q\" = 0.7 \"Gain\" = %3 } }\n"
                   "      { type = builtin name = conv%1 label = convolver config = { filename = \"%4\" } }\n"
                   "      { type = builtin name = mix%1 label = mixer control = { \"Gain 1\" = %5 \"Gain 2\" = %6 } }\n")
            .arg(c, num(advanced ? 0 : bassDb(bass)), num(advanced ? 0 : trebleDb(treble)), impulsePath, num(gains.first),
                 num(gains.second), num(advanced ? 0 : midDb(mid)));
    };
    // The ten bands are always in the graph (at 0 dB when unused): a props update can change gains, never the shape.
    auto eqNodes = [&](const QString &c) {
        QString nodes;
        for (int i = 0; i < kBandCount; ++i)
            nodes += QStringLiteral("      { type = builtin name = eq%1_%2 label = bq_peaking control = { \"Freq\" = %3 \"Q\" = %4 \"Gain\" = %5 } }\n")
                         .arg(i).arg(c).arg(num(kBandHz[i]), num(kBandQ), num(bandGain(i)));
        return nodes;
    };
    auto links = [](const QString &c) {
        return QStringLiteral(
                   "      { output = \"bass%1:Out\" input = \"mid%1:In\" }\n"
                   "      { output = \"mid%1:Out\" input = \"treb%1:In\" }\n"
                   "      { output = \"treb%1:Out\" input = \"eq0_%1:In\" }\n"
                   "      { output = \"eq9_%1:Out\" input = \"conv%1:In\" }\n"
                   "      { output = \"eq9_%1:Out\" input = \"mix%1:In 1\" }\n"
                   "      { output = \"conv%1:Out\" input = \"mix%1:In 2\" }\n")
            .arg(c)
        + [&] {
              QString chain;
              for (int i = 0; i < kBandCount - 1; ++i)
                  chain += QStringLiteral("      { output = \"eq%1_%3:Out\" input = \"eq%2_%3:In\" }\n").arg(i).arg(i + 1).arg(c);
              return chain;
          }();
    };
    return QStringLiteral(
               "context.properties = { log.level = 1 }\n"
               "context.spa-libs = { audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }\n"
               "context.modules = [\n"
               "  { name = libpipewire-module-rt args = { nice.level = -11 } flags = [ ifexists nofail ] }\n"
               "  { name = libpipewire-module-protocol-native }\n"
               "  { name = libpipewire-module-client-node }\n"
               "  { name = libpipewire-module-adapter }\n"
               "  { name = libpipewire-module-filter-chain args = {\n"
               "      node.description = \"Click Effects\"\n"
               "      media.name = \"Click Effects\"\n"
               "      filter.graph = {\n"
               "        nodes = [\n%1%2%6%7        ]\n"
               "        links = [\n%3%4        ]\n"
               "        inputs = [ \"bassL:In\" \"bassR:In\" ]\n"
               "        outputs = [ \"mixL:Out\" \"mixR:Out\" ]\n"
               "      }\n"
               "      audio.channels = 2\n"
               "      audio.position = [ FL FR ]\n"
               "      capture.props = { node.name = \"%5\" media.class = Audio/Sink priority.session = 0 priority.driver = 0 }\n"
               "      playback.props = { node.name = \"%5_out\" node.passive = true }\n"
               "  } }\n"
               "]\n")
        .arg(channel(QStringLiteral("L")), channel(QStringLiteral("R")), links(QStringLiteral("L")),
             links(QStringLiteral("R")), sinkName, eqNodes(QStringLiteral("L")), eqNodes(QStringLiteral("R")));
}

QString Effects::propsPayload(double bass, double mid, double treble, double reverb, const QVector<double> &bands) {
    const auto gains = mixGains(reverb);
    const bool advanced = bands.size() == kBandCount;
    QStringList items;
    for (const QString c : {QStringLiteral("L"), QStringLiteral("R")}) {
        items << QStringLiteral("\"bass%1:Gain\" %2").arg(c, num(advanced ? 0 : bassDb(bass)));
        items << QStringLiteral("\"mid%1:Gain\" %2").arg(c, num(advanced ? 0 : midDb(mid)));
        items << QStringLiteral("\"treb%1:Gain\" %2").arg(c, num(advanced ? 0 : trebleDb(treble)));
        for (int i = 0; i < kBandCount; ++i)
            items << QStringLiteral("\"eq%1_%2:Gain\" %3").arg(i).arg(c, num(advanced ? clampUnit(bands.at(i), -kMaxBandDb, kMaxBandDb) : 0.0));
        items << QStringLiteral("\"mix%1:Gain 1\" %2").arg(c, num(gains.first));
        items << QStringLiteral("\"mix%1:Gain 2\" %2").arg(c, num(gains.second));
    }
    return QStringLiteral("{ params = [ %1 ] }").arg(items.join(QLatin1Char(' ')));
}

// A short, dense, exponentially decaying noise tail with a fixed seed, so the
// room sounds the same every time. Mono, 16-bit, 48 kHz.
bool Effects::writeImpulseResponse(const QString &path) {
    constexpr int rate = 48000;
    constexpr int frames = rate * 8 / 5; // 1.6 s
    QByteArray pcm;
    pcm.resize(frames * 2);
    quint32 state = 0x9E3779B9u;
    for (int i = 0; i < frames; ++i) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const double noise = double(state) / 4294967295.0 * 2.0 - 1.0;
        const double t = double(i) / rate;
        const double sample = noise * qExp(-3.2 * t) * 0.3;
        const qint16 value = qint16(qRound(clampUnit(sample, -1.0, 1.0) * 32767.0));
        pcm[i * 2] = char(value & 0xff);
        pcm[i * 2 + 1] = char((value >> 8) & 0xff);
    }
    auto le32 = [](quint32 v) {
        QByteArray b(4, 0);
        for (int i = 0; i < 4; ++i)
            b[i] = char((v >> (8 * i)) & 0xff);
        return b;
    };
    auto le16 = [](quint16 v) {
        QByteArray b(2, 0);
        b[0] = char(v & 0xff);
        b[1] = char((v >> 8) & 0xff);
        return b;
    };
    QByteArray wav;
    wav += "RIFF" + le32(quint32(36 + pcm.size())) + "WAVEfmt " + le32(16) + le16(1) + le16(1) + le32(rate)
           + le32(rate * 2) + le16(2) + le16(16) + "data" + le32(quint32(pcm.size())) + pcm;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(wav) == wav.size();
}

qint64 Effects::parentPid(qint64 pid) {
    QFile file(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!file.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray line = file.readAll();
    // The name can contain spaces and parentheses, so read after the last ')'.
    const int close = line.lastIndexOf(')');
    if (close < 0)
        return 0;
    const QList<QByteArray> fields = line.mid(close + 2).split(' ');
    return fields.size() > 1 ? fields.at(1).toLongLong() : 0;
}

QList<int> Effects::ownedStreams(const QByteArray &json, qint64 rootPid, int fxSink,
                                 const std::function<qint64(qint64)> &parentOf) {
    QList<int> owned;
    const QJsonArray inputs = QJsonDocument::fromJson(json).array();
    for (const auto &value : inputs) {
        const QJsonObject input = value.toObject();
        if (input.value(QStringLiteral("sink")).toInt(-2) == fxSink)
            continue;
        const QJsonValue pidValue = input.value(QStringLiteral("properties")).toObject()
                                        .value(QStringLiteral("application.process.id"));
        const qint64 pid = pidValue.isString() ? pidValue.toString().toLongLong() : qint64(pidValue.toDouble());
        if (pid <= 1)
            continue;
        qint64 walk = pid;
        bool mine = false;
        for (int step = 0; step < 32 && walk > 1; ++step) {
            if (walk == rootPid) {
                mine = true;
                break;
            }
            walk = parentOf(walk);
        }
        if (mine && input.contains(QStringLiteral("index")))
            owned.append(input.value(QStringLiteral("index")).toInt());
    }
    return owned;
}

Effects::Effects(const QString &settingsPath, QObject *parent)
    : QObject(parent), m_settingsPath(settingsPath),
      m_sinkName(QStringLiteral("click_fx_%1").arg(QCoreApplication::applicationPid())) {
    m_available = !QStandardPaths::findExecutable(QStringLiteral("pipewire")).isEmpty()
                  && !QStandardPaths::findExecutable(QStringLiteral("pw-cli")).isEmpty()
                  && !QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty();

    bool wasEnabled = false;
    if (!m_settingsPath.isEmpty()) {
        QSettings settings(m_settingsPath, QSettings::IniFormat);
        m_bass = clampUnit(settings.value(QStringLiteral("effects/bass"), 0.0).toDouble(), -1.0, 1.0);
        m_mid = clampUnit(settings.value(QStringLiteral("effects/mid"), 0.0).toDouble(), -1.0, 1.0);
        m_treble = clampUnit(settings.value(QStringLiteral("effects/treble"), 0.0).toDouble(), -1.0, 1.0);
        m_reverb = clampUnit(settings.value(QStringLiteral("effects/reverb"), 0.0).toDouble(), 0.0, 1.0);
        wasEnabled = settings.value(QStringLiteral("effects/enabled"), false).toBool();
        m_advanced = settings.value(QStringLiteral("effects/advanced"), false).toBool();
        const QStringList saved = settings.value(QStringLiteral("effects/bands")).toStringList();
        for (int i = 0; i < kBandCount && i < saved.size(); ++i)
            m_bands[i] = clampUnit(saved.at(i).toDouble(), -kMaxBandDb, kMaxBandDb);
    }

    m_probe.setInterval(300);
    connect(&m_probe, &QTimer::timeout, this, &Effects::probe);
    m_route.setInterval(2000);
    connect(&m_route, &QTimer::timeout, this, &Effects::route);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_debounce, &QTimer::timeout, this, &Effects::applyParams);

    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
            m_quitting = true;
            killGraph();
        });
    }

    // Defaults to off; only comes back on if the user left it on.
    if (wasEnabled && m_available)
        QTimer::singleShot(0, this, [this] { setEnabled(true); });
}

Effects::~Effects() {
    m_quitting = true;
    killGraph();
}

void Effects::save() {
    if (m_settingsPath.isEmpty())
        return;
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("effects/bass"), m_bass);
    settings.setValue(QStringLiteral("effects/mid"), m_mid);
    settings.setValue(QStringLiteral("effects/treble"), m_treble);
    settings.setValue(QStringLiteral("effects/reverb"), m_reverb);
    settings.setValue(QStringLiteral("effects/enabled"), m_enabled);
    settings.setValue(QStringLiteral("effects/advanced"), m_advanced);
    QStringList bands;
    for (double db : m_bands)
        bands << QString::number(db, 'f', 2);
    settings.setValue(QStringLiteral("effects/bands"), bands);
}

void Effects::setAdvanced(bool on) {
    if (on == m_advanced)
        return;
    m_advanced = on;
    emit advancedChanged();
    emit presetChanged();
    save();
    m_debounce.start();
}

QVariantList Effects::bandList() const {
    QVariantList list;
    for (double db : m_bands)
        list.append(db);
    return list;
}

double Effects::band(int index) const {
    return index >= 0 && index < kBandCount ? m_bands.at(index) : 0.0;
}

void Effects::setBand(int index, double db) {
    if (index < 0 || index >= kBandCount)
        return;
    db = clampUnit(db, -kMaxBandDb, kMaxBandDb);
    if (qAbs(m_bands.at(index) - db) < 0.001)
        return;
    m_bands[index] = db;
    emit bandsChanged();
    emit presetChanged();
    save();
    if (m_advanced)
        m_debounce.start();
}

void Effects::setBass(double value) {
    value = clampUnit(value, -1.0, 1.0);
    if (qFuzzyCompare(m_bass + 2.0, value + 2.0))
        return;
    m_bass = value;
    emit bassChanged();
    emit presetChanged();
    save();
    m_debounce.start();
}

void Effects::setMid(double value) {
    value = clampUnit(value, -1.0, 1.0);
    if (qFuzzyCompare(m_mid + 2.0, value + 2.0))
        return;
    m_mid = value;
    emit midChanged();
    emit presetChanged();
    save();
    m_debounce.start();
}

void Effects::setTreble(double value) {
    value = clampUnit(value, -1.0, 1.0);
    if (qFuzzyCompare(m_treble + 2.0, value + 2.0))
        return;
    m_treble = value;
    emit trebleChanged();
    emit presetChanged();
    save();
    m_debounce.start();
}

void Effects::setReverb(double value) {
    value = clampUnit(value, 0.0, 1.0);
    if (qFuzzyCompare(m_reverb + 2.0, value + 2.0))
        return;
    m_reverb = value;
    emit reverbChanged();
    emit presetChanged();
    save();
    m_debounce.start();
}

void Effects::reset() {
    setBass(0);
    setMid(0);
    setTreble(0);
    setReverb(0);
    for (int i = 0; i < kBandCount; ++i)
        setBand(i, 0);
}

void Effects::applyShape(const QVariantList &bands, double bass, double mid, double treble, double reverb) {
    setBass(bass);
    setMid(mid);
    setTreble(treble);
    setReverb(reverb);
    for (int i = 0; i < kBandCount && i < bands.size(); ++i)
        setBand(i, bands.at(i).toDouble());
}

void Effects::setEnabled(bool on) {
    if (on == m_enabled)
        return;
    if (on && !m_available) {
        qWarning("Effects: PipeWire tools (pipewire, pw-cli, pactl) were not found");
        return;
    }
    m_enabled = on;
    emit enabledChanged();
    save();
    if (on)
        startGraph();
    else
        stopGraph(true);
}

void Effects::run(const QString &program, const QStringList &args, std::function<void(int, const QByteArray &)> done) {
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        if (done)
            done(status == QProcess::NormalExit ? code : -1, process->readAllStandardOutput());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (done)
            done(-1, {});
        process->deleteLater();
    });
    process->setStandardErrorFile(QProcess::nullDevice());
    process->start(program, args);
}

void Effects::startGraph() {
    if (m_graph) {
        // Switched back on while the old graph was still winding down: reuse it.
        m_probeTries = 0;
        m_probing = false;
        m_probe.start();
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString impulse = dir + QStringLiteral("/click-room.wav");
    if (!QFile::exists(impulse) && !writeImpulseResponse(impulse)) {
        fail(QStringLiteral("could not write the reverb impulse response"));
        return;
    }
    const QString confPath = dir + QStringLiteral("/click-fx.conf");
    QFile conf(confPath);
    if (!conf.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || conf.write(filterChainConfig(impulse, m_bass, m_mid, m_treble, m_reverb, m_sinkName, m_advanced ? m_bands : QVector<double>()).toUtf8()) < 0) {
        fail(QStringLiteral("could not write the effects graph"));
        return;
    }
    conf.close();

    m_graph = new QProcess(this);
    // If Click dies without cleaning up, the graph must not outlive it.
    m_graph->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
    m_graph->setStandardOutputFile(QProcess::nullDevice());
    m_graph->setStandardErrorFile(QProcess::nullDevice());
    connect(m_graph, &QProcess::finished, this, [this] { graphExited(); });
    connect(m_graph, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            graphExited();
    });
    m_graph->start(QStringLiteral("pipewire"), {QStringLiteral("-c"), confPath});
    m_probeTries = 0;
    m_probing = false;
    m_probe.start();
}

// Waits until PipeWire shows the click_fx sink, then learns its node id.
void Effects::probe() {
    if (m_probing || m_ready || !m_enabled)
        return;
    if (++m_probeTries > 25) {
        fail(QStringLiteral("the effects sink never appeared"));
        return;
    }
    m_probing = true;
    run(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sinks")},
        [this](int code, const QByteArray &out) {
            int sinkIndex = -1;
            if (code == 0) {
                const QJsonArray sinks = QJsonDocument::fromJson(out).array();
                for (const auto &value : sinks) {
                    const QJsonObject sink = value.toObject();
                    if (sink.value(QStringLiteral("name")).toString() == m_sinkName)
                        sinkIndex = sink.value(QStringLiteral("index")).toInt(-1);
                }
            }
            if (sinkIndex < 0 || !m_enabled) {
                m_probing = false;
                return;
            }
            run(QStringLiteral("pw-dump"), {}, [this, sinkIndex](int dumpCode, const QByteArray &dump) {
                int nodeId = -1;
                if (dumpCode == 0) {
                    const QJsonArray objects = QJsonDocument::fromJson(dump).array();
                    for (const auto &value : objects) {
                        const QJsonObject object = value.toObject();
                        const QJsonObject props = object.value(QStringLiteral("info")).toObject()
                                                      .value(QStringLiteral("props")).toObject();
                        if (props.value(QStringLiteral("node.name")).toString() == m_sinkName)
                            nodeId = object.value(QStringLiteral("id")).toInt(-1);
                    }
                }
                m_probing = false;
                if (nodeId >= 0 && m_enabled && !m_ready)
                    becameReady(sinkIndex, nodeId);
            });
        });
}

void Effects::becameReady(int sinkIndex, int nodeId) {
    m_probe.stop();
    m_sinkIndex = sinkIndex;
    m_nodeId = nodeId;
    m_ready = true;
    emit activeChanged();
    applyParams();
    route();
    m_route.start();
}

void Effects::applyParams() {
    if (!m_ready)
        return;
    if (m_applying) {
        m_dirty = true;
        return;
    }
    m_applying = true;
    run(QStringLiteral("pw-cli"),
        {QStringLiteral("s"), QString::number(m_nodeId), QStringLiteral("Props"), propsPayload(m_bass, m_mid, m_treble, m_reverb, m_advanced ? m_bands : QVector<double>())},
        [this](int, const QByteArray &) {
            m_applying = false;
            if (m_dirty) {
                m_dirty = false;
                applyParams();
            }
        });
}

// Moves Click's own playback stream onto the effects sink. A new stream
// appears with every new page load, so this keeps checking while on.
void Effects::route() {
    if (!m_ready || m_routing)
        return;
    m_routing = true;
    run(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sink-inputs")},
        [this](int code, const QByteArray &out) {
            m_routing = false;
            if (code != 0 || !m_ready)
                return;
            const auto ids = ownedStreams(out, QCoreApplication::applicationPid(), m_sinkIndex, &Effects::parentPid);
            for (int id : ids)
                run(QStringLiteral("pactl"), {QStringLiteral("move-sink-input"), QString::number(id), m_sinkName});
        });
}

void Effects::stopGraph(bool moveStreamsBack) {
    m_probe.stop();
    m_route.stop();
    m_debounce.stop();
    const bool wasReady = m_ready;
    const int fxSink = m_sinkIndex;
    m_ready = false;
    m_sinkIndex = -1;
    m_nodeId = -1;
    m_probing = false;
    if (wasReady)
        emit activeChanged();

    if (!moveStreamsBack || !wasReady || m_quitting) {
        killGraph();
        return;
    }
    // Put our stream back on the normal output first so the sound never drops,
    // then take the graph down.
    run(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sink-inputs")},
        [this, fxSink](int code, const QByteArray &out) {
            if (code == 0) {
                // Streams on the fx sink are the ones to return; pass -2 so none is skipped, then filter.
                const QJsonArray inputs = QJsonDocument::fromJson(out).array();
                const auto mine = ownedStreams(out, QCoreApplication::applicationPid(), -2, &Effects::parentPid);
                for (const auto &value : inputs) {
                    const QJsonObject input = value.toObject();
                    const int id = input.value(QStringLiteral("index")).toInt(-1);
                    if (input.value(QStringLiteral("sink")).toInt(-2) == fxSink && mine.contains(id))
                        run(QStringLiteral("pactl"),
                            {QStringLiteral("move-sink-input"), QString::number(id), QStringLiteral("@DEFAULT_SINK@")});
                }
            }
            QTimer::singleShot(400, this, [this] {
                if (!m_enabled)
                    killGraph();
            });
        });
}

void Effects::killGraph() {
    if (!m_graph)
        return;
    QProcess *graph = m_graph;
    m_graph = nullptr;
    graph->disconnect(this);
    graph->terminate();
    if (!graph->waitForFinished(1500)) {
        graph->kill();
        graph->waitForFinished(500);
    }
    delete graph;
}

void Effects::graphExited() {
    if (m_quitting || !m_graph)
        return;
    fail(QStringLiteral("the effects graph stopped"));
}

void Effects::fail(const QString &why) {
    qWarning("Effects: %s", qPrintable(why));
    m_probe.stop();
    m_route.stop();
    const bool wasReady = m_ready;
    m_ready = false;
    m_sinkIndex = -1;
    m_nodeId = -1;
    m_probing = false;
    killGraph();
    if (wasReady)
        emit activeChanged();
    if (m_enabled) {
        m_enabled = false;
        emit enabledChanged();
        save();
    }
}
