#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QTimer>
#include <functional>

// A three-band EQ (bass, mid, treble) and reverb for Click's own audio.
//
// Apple's audio is DRM-protected inside the web page, so it cannot go through
// Web Audio. Instead Click runs a small PipeWire filter-chain (a virtual sink
// called click_fx that plays into the normal output) and moves only its own
// playback stream onto it. Nothing else on the system is touched.
class Effects : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(double bass READ bass WRITE setBass NOTIFY bassChanged)
    Q_PROPERTY(double mid READ mid WRITE setMid NOTIFY midChanged)
    Q_PROPERTY(double treble READ treble WRITE setTreble NOTIFY trebleChanged)
    Q_PROPERTY(double reverb READ reverb WRITE setReverb NOTIFY reverbChanged)
    Q_PROPERTY(QStringList presets READ presets CONSTANT)
    Q_PROPERTY(QString preset READ preset NOTIFY presetChanged)
    // Advanced mode: ten peaking bands (31 Hz to 16 kHz, -12 to +12 dB) replace the three simple knobs.
    Q_PROPERTY(bool advanced READ advanced WRITE setAdvanced NOTIFY advancedChanged)
    Q_PROPERTY(QVariantList bands READ bandList NOTIFY bandsChanged)
    Q_PROPERTY(QVariantList bandFrequencies READ bandFrequencies CONSTANT)

public:
    // An empty settingsPath keeps everything in memory (used by tests).
    explicit Effects(const QString &settingsPath = {}, QObject *parent = nullptr);
    ~Effects() override;

    bool available() const { return m_available; }
    bool enabled() const { return m_enabled; }
    bool active() const { return m_ready; }
    double bass() const { return m_bass; }
    double mid() const { return m_mid; }
    double treble() const { return m_treble; }
    double reverb() const { return m_reverb; }

    static constexpr int kBandCount = 10;
    static constexpr double kMaxBandDb = 12.0;
    static const double *bandHz();
    bool advanced() const { return m_advanced; }
    void setAdvanced(bool on);
    QVariantList bandList() const;
    static QVariantList bandFrequencies();
    // Gain in dB of band `index`; setBand clamps to +-12 dB.
    Q_INVOKABLE double band(int index) const;
    Q_INVOKABLE void setBand(int index, double db);
    QVector<double> bandGains() const { return m_bands; }

    void setEnabled(bool on);
    void setBass(double value);
    void setMid(double value);
    void setTreble(double value);
    void setReverb(double value);
    Q_INVOKABLE void reset();
    // Sets both modes at once from a described sound: the ten bands (dB), the three simple values, and reverb.
    Q_INVOKABLE void applyShape(const QVariantList &bands, double bass, double mid, double treble, double reverb);

    // Named starting points. `preset` is the one the sliders currently match, or empty.
    static QStringList presets();
    QString preset() const;
    Q_INVOKABLE void applyPreset(const QString &name);
    // Bass, mid, treble, reverb for a preset; false for an unknown name.
    static bool presetValues(const QString &name, double &bass, double &mid, double &treble, double &reverb);
    // The ten band gains (dB) of a preset; false for an unknown name.
    static bool presetBands(const QString &name, QVector<double> &bands);
    // The EQ's gain in dB at `points` frequencies spaced evenly on a log scale
    // from 20 Hz to 20 kHz, for drawing the curve.
    Q_INVOKABLE QVariantList curve(int points) const;
    // `bands` (ten gains in dB) replace the simple three when given and non-empty.
    static double responseDb(double bass, double mid, double treble, double hz, const QVector<double> &bands = {});

    // ---- pure helpers, exposed for tests ----
    static double bassDb(double value);
    static double trebleDb(double value);
    static double midDb(double value);
    // Gains for the dry and wet inputs of the mixer for a 0..1 reverb amount.
    static QPair<double, double> mixGains(double reverb);
    // The virtual sink is named per process, so two instances never share one.
    QString sinkName() const { return m_sinkName; }
    static QString filterChainConfig(const QString &impulsePath, double bass, double mid, double treble, double reverb,
                                     const QString &sinkName = QStringLiteral("click_fx"), const QVector<double> &bands = {});
    static QString propsPayload(double bass, double mid, double treble, double reverb, const QVector<double> &bands = {});
    static bool writeImpulseResponse(const QString &path);
    // Indexes of sink inputs that belong to this process or its children and
    // are not already on `fxSink`. `parentOf` maps a pid to its parent pid.
    static QList<int> ownedStreams(const QByteArray &sinkInputsJson, qint64 rootPid, int fxSink,
                                   const std::function<qint64(qint64)> &parentOf);
    static qint64 parentPid(qint64 pid);

signals:
    void enabledChanged();
    void activeChanged();
    void bassChanged();
    void midChanged();
    void presetChanged();
    void advancedChanged();
    void bandsChanged();
    void trebleChanged();
    void reverbChanged();

private:
    void run(const QString &program, const QStringList &args,
             std::function<void(int, const QByteArray &)> done = {});
    void startGraph();
    void probe();
    void becameReady(int sinkIndex, int nodeId);
    void applyParams();
    void route();
    void stopGraph(bool moveStreamsBack);
    void killGraph();
    void graphExited();
    void fail(const QString &why);
    void save();

    QString m_settingsPath;
    QString m_sinkName;
    bool m_available = false;
    bool m_enabled = false;
    bool m_ready = false;
    double m_bass = 0;
    double m_mid = 0;
    double m_treble = 0;
    double m_reverb = 0;
    bool m_advanced = false;
    QVector<double> m_bands = QVector<double>(kBandCount, 0.0);

    QProcess *m_graph = nullptr;
    QTimer m_probe;
    QTimer m_route;
    QTimer m_debounce;
    int m_probeTries = 0;
    bool m_probing = false;
    int m_sinkIndex = -1;
    int m_nodeId = -1;
    bool m_applying = false;
    bool m_dirty = false;
    bool m_routing = false;
    bool m_quitting = false;
};
