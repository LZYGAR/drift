#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QTimer>
#include <QVector>
#include <array>
#include <functional>
#include <vector>

// Turns raw mono samples into what the animations need, 60 times a second:
// how loud it is, how much bass, mid and treble there is, a 24-band spectrum,
// and *onsets*: the moment a kick, a snare or a hi-hat lands.
//
// Onsets are found where they happen, not by waiting for a smoothed level to
// cross a line. A kick is the sudden rise of a low-passed envelope; a snare
// and a hat are sudden rises across their bands (spectral flux); each is
// judged against what was normal over the last second, so a steady bass line
// is not mistaken for beats and a quiet song still gets them.
//
// Values are 0 to 1 and are scaled against the loudest recent sound, so the
// animation looks the same at any volume setting. They rise at once and fall
// over a few frames; the smoothing the eye sees is done later, by springs.
class LevelAnalyzer {
public:
    static constexpr int kBands = 24;
    static constexpr int kRate = 44100;
    static constexpr int kHop = kRate / 60; // samples per frame

    struct Frame {
        double level = 0;
        double bass = 0;
        double mid = 0;
        double treble = 0;
        // 0 on most frames; on the frame an onset lands, how strong it is (0.5 to 1).
        double kick = 0;
        double snare = 0;
        double hat = 0;
        std::array<double, kBands> bands{};
    };

    LevelAnalyzer();

    // Feeds mono samples. Returns how many frames were finished; frame() is the latest.
    // `out`, when given, receives every finished frame (an onset is never lost when several finish at once).
    int feed(const qint16 *samples, int count, std::vector<Frame> *out = nullptr);
    const Frame &frame() const { return m_frame; }
    void reset();
    // Centre frequency of a band in Hz: 40 Hz up to 16 kHz, evenly spaced on a log scale.
    static double bandCentre(int band);

private:
    // Decides when a stream of "how fast is it rising" numbers contains an onset.
    struct Onset {
        std::array<double, 40> history{};
        int next = 0;
        int cooldown = 0;
        double previous = 0;
        double peak = 0;
        // `flux` is the rise this frame; `floor` is the smallest rise worth reacting to.
        double step(double flux, double floor, int cooldownFrames);
    };

    void finishFrame();
    static double decibels(double rms);

    double m_b0[kBands], m_b2[kBands], m_a1[kBands], m_a2[kBands];
    double m_z1[kBands] = {}, m_z2[kBands] = {};
    double m_sumSquares[kBands] = {};
    double m_totalSquares = 0;
    int m_inFrame = 0;
    double m_bandPeak = 0.45;
    double m_levelPeak = 0.45;

    // Kick: two low-pass stages at 130 Hz, and the peak envelope of what is left.
    double m_lpB0 = 0, m_lpB1 = 0, m_lpA1 = 0, m_lpA2 = 0;
    double m_lpZ[2][2] = {};
    double m_env = 0;
    double m_envAtHop = 0;
    double m_envAttack = 0, m_envRelease = 0;
    Onset m_kick, m_snare, m_hat;
    std::array<double, kBands> m_previousShaped{};
    Frame m_frame;
};

// Listens to this app's own playback through PipeWire, so the visuals can
// follow the music.
//
// Apple's audio is DRM-protected inside the page and cannot be read there.
// But once it is playing, it is an ordinary stream on the sound server, and
// `parec --monitor-stream` can tap that one stream (and nothing else) without
// changing what you hear. Only levels are kept; no audio is stored or sent.
//
// Two things make it feel in time:
//  * What is shown is *delayed* to match the speakers. Bluetooth sound arrives
//    well after the computer plays it (around 150 to 250 ms), so frames wait in
//    a short queue for `syncMs` before they are shown. The right delay is kept
//    per output device, with a sensible start for Bluetooth.
//  * Values are advanced once per drawn frame (`advance()`, called from the
//    frame clock), with real elapsed time, so motion does not stutter against
//    the screen's refresh rate.
//
// It only listens while `wanted` is true (a song is playing and the window is
// showing). When it cannot listen, `active` stays false and the interface
// falls back to its own animation.
class AudioLevels : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool wanted READ wanted WRITE setWanted NOTIFY wantedChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(double level READ level NOTIFY levelsChanged)
    Q_PROPERTY(double bass READ bass NOTIFY levelsChanged)
    Q_PROPERTY(double mid READ mid NOTIFY levelsChanged)
    Q_PROPERTY(double treble READ treble NOTIFY levelsChanged)
    Q_PROPERTY(double beat READ beat NOTIFY levelsChanged)
    Q_PROPERTY(double snare READ snare NOTIFY levelsChanged)
    Q_PROPERTY(double hat READ hat NOTIFY levelsChanged)
    Q_PROPERTY(double cover READ cover NOTIFY levelsChanged)
    Q_PROPERTY(QList<double> spectrum READ spectrum NOTIFY levelsChanged)
    Q_PROPERTY(int syncMs READ syncMs WRITE setSyncMs NOTIFY syncChanged)
    Q_PROPERTY(QString sinkName READ sinkName NOTIFY syncChanged)
    Q_PROPERTY(bool bluetooth READ bluetooth NOTIFY syncChanged)
    // Tap to sync: tap along with the kick you hear; the delay is worked out from the taps.
    Q_PROPERTY(bool tapping READ tapping NOTIFY tapChanged)
    Q_PROPERTY(int tapCount READ tapCount NOTIFY tapChanged)
    Q_PROPERTY(QString tapStatus READ tapStatus NOTIFY tapChanged)

public:
    // `synthetic` makes up a steady 120 bpm groove instead of listening, for
    // screenshots and tests that must not depend on a sound server.
    explicit AudioLevels(bool synthetic = false, QObject *parent = nullptr);
    ~AudioLevels() override;

    bool wanted() const { return m_wanted; }
    void setWanted(bool wanted);
    bool active() const { return m_active; }
    // What is shown right now: smoothed, delayed to match the speakers.
    const LevelAnalyzer::Frame &frame() const { return m_frame; }
    double level() const { return m_frame.level; }
    double bass() const { return m_frame.bass; }
    double mid() const { return m_frame.mid; }
    double treble() const { return m_frame.treble; }
    double beat() const { return m_frame.kick; }
    double snare() const { return m_frame.snare; }
    // How hard the album cover should move, 0 to 1. With bass in the music it follows the bass and
    // ignores small wiggles; in a passage with no bass it becomes very sensitive to everything else.
    double cover() const { return m_cover; }
    double hat() const { return m_frame.hat; }
    QList<double> spectrum() const { return QList<double>(m_frame.bands.begin(), m_frame.bands.end()); }

    // The delay between hearing a beat on this computer and hearing it from the speakers.
    int syncMs() const { return m_syncMs; }
    void setSyncMs(int milliseconds);
    QString sinkName() const { return m_sinkName; }
    bool bluetooth() const { return isBluetooth(m_sinkName); }
    // Where the per-device delays are kept (a QSettings ini file); empty keeps them in memory.
    void setPrefsPath(const QString &path);
    // Pretend the sound goes to this output (for tests).
    void useSink(const QString &name);
    static bool isBluetooth(const QString &sinkName);
    static int defaultSyncMs(const QString &sinkName);

    bool tapping() const { return m_tapping; }
    int tapCount() const { return int(m_taps.size()); }
    QString tapStatus() const { return m_tapStatus; }
    Q_INVOKABLE void startTapSync();
    Q_INVOKABLE void tap();
    Q_INVOKABLE void stopTapSync();
    static constexpr int kTapsNeeded = 8;
    // The speaker delay in ms from the times kicks were heard here and the times the listener tapped
    // (both in seconds on one clock). Each tap is matched to the latest kick before it; at least 4
    // matched taps are needed and taps far from the rest are dropped. -1 when they do not agree.
    static int syncFromTaps(const QVector<double> &kicks, const QVector<double> &taps, int *used = nullptr);
    void heardKickForTest(double at) { m_heardKicks.append(at); }
    void tapForTest(double at) { m_taps.append(at); }
    int finishTapsForTest() { return finishTaps(); }

    // Recent kicks as (seconds ago, strength), newest first: the beat waves travelling along the bar.
    QVector<QPair<double, double>> kickPulses() const;

    // Moves what is shown on by the time that really passed. Called once per drawn frame.
    Q_INVOKABLE void advance();
    // Tests drive time by hand: `now` is in seconds, frames are shown once their `due` time has come.
    void injectForTest(const LevelAnalyzer::Frame &frame, double due);
    void stepForTest(double now) { step(now); }

    // The stream of this process (or one of its children) to listen to, from
    // `pactl -f json list sink-inputs`. Streams that are playing beat ones that
    // are paused. Returns its index, or -1, and the index of the sink it plays on.
    static int chooseStream(const QByteArray &sinkInputsJson, qint64 rootPid,
                            const std::function<qint64(qint64)> &parentOf, int &sinkIndex);
    static QStringList captureArguments(const QString &sinkName, int streamIndex);

signals:
    void wantedChanged();
    void activeChanged();
    void levelsChanged();
    void syncChanged();
    void tapChanged();
    // A kick was just shown, with its strength (0.5 to 1).
    void beatShown(double strength);
    // The cover just took a clear hit (a strong kick, or in a bass-free passage any clear sound).
    void coverHit(double strength);

private:
    struct Timed {
        double due; // seconds on m_clock
        LevelAnalyzer::Frame frame;
    };
    struct Pulse {
        double time;
        double strength;
    };

    void scan();
    void startCapture(const QString &sinkName, int stream, int sinkIndex);
    void stopCapture();
    void onData();
    void setActive(bool active);
    void run(const QString &program, const QStringList &args, std::function<void(int, const QByteArray &)> done);
    void synthesize();
    bool step(double now);
    double nowSeconds() const { return m_clock.nsecsElapsed() / 1e9; }
    void loadSync();
    void noteHeardKicks(const LevelAnalyzer::Frame &frame, double heardAt);
    int finishTaps();
    QVector<double> m_heardKicks; // when kicks reached this computer (before any delay), seconds
    QVector<double> m_taps;
    bool m_tapping = false;
    QString m_tapStatus;

    bool m_synthetic;
    bool m_wanted = false;
    bool m_active = false;
    bool m_missing = false;
    bool m_scanning = false;
    LevelAnalyzer m_analyzer;
    LevelAnalyzer::Frame m_frame;   // what is shown
    LevelAnalyzer::Frame m_target;  // what the latest due frame said
    QVector<Timed> m_queue;
    QVector<Pulse> m_pulses;
    double m_lastAdvance = 0;
    // Slow averages of the bands, so the cover can tell a real change from the usual wiggle.
    double m_bassAvg = 0, m_midAvg = 0, m_trebleAvg = 0;
    bool m_averagesStarted = false; // they start from the first real values, not from zero
    double m_cover = 0;
    bool m_coverHigh = false;
    double m_lastCoverHit = -10;
    qint64 m_synthStep = -1;
    int m_scanTick = 0;
    QProcess *m_capture = nullptr;
    QByteArray m_pending;
    int m_stream = -1;
    int m_sink = -1;
    QString m_sinkName;
    QString m_prefsPath;
    int m_syncMs = 0;
    bool m_syncChosen = false;
    QTimer m_scan;
    QTimer m_watch;
    QTimer m_synth;
    QTimer m_fade;
    QElapsedTimer m_lastFrame;
    QElapsedTimer m_clock;
};
