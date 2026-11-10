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

#include <csignal>
#include <sys/prctl.h>

namespace {

const QString kSink = QStringLiteral("click_fx");
constexpr double kMaxShelfDb = 10.0;

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

QPair<double, double> Effects::mixGains(double reverb) {
    const double wet = clampUnit(reverb, 0.0, 1.0);
    // The dry signal ducks a little as the room grows so the level stays even.
    return {1.0 - 0.35 * wet, 0.9 * wet};
}

QString Effects::filterChainConfig(const QString &impulsePath, double bass, double treble, double reverb) {
    const auto gains = mixGains(reverb);
    auto channel = [&](const QString &c) {
        return QStringLiteral(
                   "      { type = builtin name = bass%1 label = bq_lowshelf control = { \"Freq\" = 100.0 \"Q\" = 0.7 \"Gain\" = %2 } }\n"
                   "      { type = builtin name = treb%1 label = bq_highshelf control = { \"Freq\" = 8000.0 \"Q\" = 0.7 \"Gain\" = %3 } }\n"
                   "      { type = builtin name = conv%1 label = convolver config = { filename = \"%4\" } }\n"
                   "      { type = builtin name = mix%1 label = mixer control = { \"Gain 1\" = %5 \"Gain 2\" = %6 } }\n")
            .arg(c, num(bassDb(bass)), num(trebleDb(treble)), impulsePath, num(gains.first), num(gains.second));
    };
    auto links = [](const QString &c) {
        return QStringLiteral(
                   "      { output = \"bass%1:Out\" input = \"treb%1:In\" }\n"
                   "      { output = \"treb%1:Out\" input = \"conv%1:In\" }\n"
                   "      { output = \"treb%1:Out\" input = \"mix%1:In 1\" }\n"
                   "      { output = \"conv%1:Out\" input = \"mix%1:In 2\" }\n")
            .arg(c);
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
               "        nodes = [\n%1%2        ]\n"
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
             links(QStringLiteral("R")), kSink);
}

QString Effects::propsPayload(double bass, double treble, double reverb) {
    const auto gains = mixGains(reverb);
    QStringList items;
    for (const QString c : {QStringLiteral("L"), QStringLiteral("R")}) {
        items << QStringLiteral("\"bass%1:Gain\" %2").arg(c, num(bassDb(bass)));
        items << QStringLiteral("\"treb%1:Gain\" %2").arg(c, num(trebleDb(treble)));
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

Effects::Effects(const QString &settingsPath, QObject *parent) : QObject(parent), m_settingsPath(settingsPath) {
    m_available = !QStandardPaths::findExecutable(QStringLiteral("pipewire")).isEmpty()
                  && !QStandardPaths::findExecutable(QStringLiteral("pw-cli")).isEmpty()
                  && !QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty();

    bool wasEnabled = false;
    if (!m_settingsPath.isEmpty()) {
        QSettings settings(m_settingsPath, QSettings::IniFormat);
        m_bass = clampUnit(settings.value(QStringLiteral("effects/bass"), 0.0).toDouble(), -1.0, 1.0);
        m_treble = clampUnit(settings.value(QStringLiteral("effects/treble"), 0.0).toDouble(), -1.0, 1.0);
        m_reverb = clampUnit(settings.value(QStringLiteral("effects/reverb"), 0.0).toDouble(), 0.0, 1.0);
        wasEnabled = settings.value(QStringLiteral("effects/enabled"), false).toBool();
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
    settings.setValue(QStringLiteral("effects/treble"), m_treble);
    settings.setValue(QStringLiteral("effects/reverb"), m_reverb);
    settings.setValue(QStringLiteral("effects/enabled"), m_enabled);
}

void Effects::setBass(double value) {
    value = clampUnit(value, -1.0, 1.0);
    if (qFuzzyCompare(m_bass + 2.0, value + 2.0))
        return;
    m_bass = value;
    emit bassChanged();
    save();
    m_debounce.start();
}

void Effects::setTreble(double value) {
    value = clampUnit(value, -1.0, 1.0);
    if (qFuzzyCompare(m_treble + 2.0, value + 2.0))
        return;
    m_treble = value;
    emit trebleChanged();
    save();
    m_debounce.start();
}

void Effects::setReverb(double value) {
    value = clampUnit(value, 0.0, 1.0);
    if (qFuzzyCompare(m_reverb + 2.0, value + 2.0))
        return;
    m_reverb = value;
    emit reverbChanged();
    save();
    m_debounce.start();
}

void Effects::reset() {
    setBass(0);
    setTreble(0);
    setReverb(0);
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
        || conf.write(filterChainConfig(impulse, m_bass, m_treble, m_reverb).toUtf8()) < 0) {
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
                    if (sink.value(QStringLiteral("name")).toString() == kSink)
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
                        if (props.value(QStringLiteral("node.name")).toString() == kSink)
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
        {QStringLiteral("s"), QString::number(m_nodeId), QStringLiteral("Props"), propsPayload(m_bass, m_treble, m_reverb)},
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
                run(QStringLiteral("pactl"), {QStringLiteral("move-sink-input"), QString::number(id), kSink});
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
