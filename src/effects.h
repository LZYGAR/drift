#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <functional>

// Bass, treble and reverb for Click's own audio.
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
    Q_PROPERTY(double treble READ treble WRITE setTreble NOTIFY trebleChanged)
    Q_PROPERTY(double reverb READ reverb WRITE setReverb NOTIFY reverbChanged)

public:
    // An empty settingsPath keeps everything in memory (used by tests).
    explicit Effects(const QString &settingsPath = {}, QObject *parent = nullptr);
    ~Effects() override;

    bool available() const { return m_available; }
    bool enabled() const { return m_enabled; }
    bool active() const { return m_ready; }
    double bass() const { return m_bass; }
    double treble() const { return m_treble; }
    double reverb() const { return m_reverb; }

    void setEnabled(bool on);
    void setBass(double value);
    void setTreble(double value);
    void setReverb(double value);
    Q_INVOKABLE void reset();

    // ---- pure helpers, exposed for tests ----
    static double bassDb(double value);
    static double trebleDb(double value);
    // Gains for the dry and wet inputs of the mixer for a 0..1 reverb amount.
    static QPair<double, double> mixGains(double reverb);
    static QString filterChainConfig(const QString &impulsePath, double bass, double treble, double reverb);
    static QString propsPayload(double bass, double treble, double reverb);
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
    bool m_available = false;
    bool m_enabled = false;
    bool m_ready = false;
    double m_bass = 0;
    double m_treble = 0;
    double m_reverb = 0;

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
