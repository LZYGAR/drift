#include "desktop.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

namespace {
// The gap kept from the screen's edges, like Hyprland's own gaps.
constexpr int kMargin = 14;
// The two sizes of the window (MainWindow.qml's miniHeight and fullHeight, and its fixed width).
constexpr int kWidth = 360;
constexpr int kMiniHeight = 144;
constexpr int kFullHeight = 640;
} // namespace

Desktop::Desktop(const QString &prefsPath, bool enabled, QObject *parent) : QObject(parent), m_prefsPath(prefsPath) {
    const QByteArray signature = qgetenv("HYPRLAND_INSTANCE_SIGNATURE");
    m_available = enabled && !signature.isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("hyprctl")).isEmpty();
    if (!m_prefsPath.isEmpty()) {
        QSettings prefs(m_prefsPath, QSettings::IniFormat);
        m_follow = prefs.value(QStringLiteral("desktop/follow"), true).toBool();
        if (prefs.contains(QStringLiteral("desktop/parkX")) && prefs.contains(QStringLiteral("desktop/parkY"))) {
            m_hasPark = true;
            m_parkX = prefs.value(QStringLiteral("desktop/parkX")).toInt();
            m_parkY = prefs.value(QStringLiteral("desktop/parkY")).toInt();
        }
    }
    // Desktop switches arrive in quick bursts (holding the key); act once they settle.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(90);
    connect(&m_debounce, &QTimer::timeout, this, &Desktop::evaluate);
    m_reconnect.setSingleShot(true);
    m_reconnect.setInterval(3000);
    connect(&m_reconnect, &QTimer::timeout, this, &Desktop::connectEvents);
    connect(&m_events, &QLocalSocket::readyRead, this, &Desktop::readEvents);
    connect(&m_events, &QLocalSocket::disconnected, this, [this] {
        if (m_available)
            m_reconnect.start();
    });
    if (m_available)
        connectEvents();
}

Desktop::~Desktop() {
    // Never leave the player pinned to every desktop behind when it quits.
    if (m_away && !m_address.isEmpty()) {
        QProcess process;
        process.start(QStringLiteral("hyprctl"), {QStringLiteral("dispatch"), pinCommand(m_address, false)});
        process.waitForFinished(800);
    }
}

void Desktop::setFollow(bool on) {
    if (on == m_follow)
        return;
    m_follow = on;
    if (!m_prefsPath.isEmpty()) {
        QSettings prefs(m_prefsPath, QSettings::IniFormat);
        prefs.setValue(QStringLiteral("desktop/follow"), on);
    }
    emit followChanged();
    // Turning it off while the player is parked away brings it home at once.
    if (m_available)
        m_debounce.start();
}

// Whether the window was made mini by us, or you changed it yourself while it was parked.
void Desktop::setMini(bool mini) {
    if (mini == m_mini)
        return;
    m_mini = mini;
    if (m_away && mini != m_expectedMini) {
        m_autoMini = false; // you took over: it stays as you left it
        m_expectedMini = mini;
    }
}

void Desktop::setAway(bool away) {
    if (away == m_away)
        return;
    m_away = away;
    emit awayChanged();
}

// ---- the decisions ----------------------------------------------------------------------------

Desktop::Step Desktop::decide(const State &s) {
    if (s.awayByUs) {
        // Home again (or the feature switched off): put it back. Anywhere else: leave it parked.
        return (!s.follow || s.activeWorkspace == s.home) ? Step::ComeHome : Step::KeepAway;
    }
    if (!s.follow || !s.playing || s.pinned || s.hidden || s.special)
        return Step::Nothing;
    // Only when the desktop being looked at is not the one the window is on.
    return s.activeWorkspace != s.clientWorkspace && s.activeWorkspace > 0 ? Step::GoAway : Step::Nothing;
}

int Desktop::workspaceFromEvent(const QByteArray &line) {
    static const QByteArray prefix = "workspacev2>>";
    if (!line.startsWith(prefix))
        return -1;
    bool ok = false;
    const int id = line.mid(prefix.size()).split(',').value(0).toInt(&ok);
    return ok ? id : -1;
}

QPair<int, int> Desktop::topLeft(const QJsonObject &monitor, int margin) {
    const QJsonArray reserved = monitor.value(QStringLiteral("reserved")).toArray(); // left, top, right, bottom
    return {monitor.value(QStringLiteral("x")).toInt() + reserved.at(0).toInt() + margin,
            monitor.value(QStringLiteral("y")).toInt() + reserved.at(1).toInt() + margin};
}

QPair<int, int> Desktop::clampInside(const QJsonObject &monitor, int x, int y, int width, int height) {
    const QJsonArray reserved = monitor.value(QStringLiteral("reserved")).toArray(); // left, top, right, bottom
    const double scale = qMax(0.1, monitor.value(QStringLiteral("scale")).toDouble(1.0));
    const int left = monitor.value(QStringLiteral("x")).toInt() + reserved.at(0).toInt();
    const int top = monitor.value(QStringLiteral("y")).toInt() + reserved.at(1).toInt();
    const int right = monitor.value(QStringLiteral("x")).toInt() + int(monitor.value(QStringLiteral("width")).toDouble() / scale) - reserved.at(2).toInt();
    const int bottom = monitor.value(QStringLiteral("y")).toInt() + int(monitor.value(QStringLiteral("height")).toDouble() / scale) - reserved.at(3).toInt();
    return {qBound(left, x, qMax(left, right - width)), qBound(top, y, qMax(top, bottom - height))};
}

QPair<int, int> Desktop::parkSpot(const QJsonObject &monitor, int margin, bool remembered, int offsetX, int offsetY) {
    const auto corner = topLeft(monitor, 0);
    if (!remembered)
        return {corner.first + margin, corner.second + margin};
    // The mini player is about 360 by 144; keep it fully on this monitor.
    const double scale = qMax(0.1, monitor.value(QStringLiteral("scale")).toDouble(1.0));
    const int width = int(monitor.value(QStringLiteral("width")).toDouble() / scale);
    const int height = int(monitor.value(QStringLiteral("height")).toDouble() / scale);
    const int maxX = monitor.value(QStringLiteral("x")).toInt() + qMax(0, width - 360);
    const int maxY = monitor.value(QStringLiteral("y")).toInt() + qMax(0, height - 144);
    return {qBound(corner.first, corner.first + offsetX, qMax(corner.first, maxX)),
            qBound(corner.second, corner.second + offsetY, qMax(corner.second, maxY))};
}

QString Desktop::pinCommand(const QString &address, bool on) {
    return QStringLiteral("hl.dsp.window.pin({ action = \"%1\", window = \"address:%2\" })").arg(on ? QStringLiteral("on") : QStringLiteral("off"), address);
}

// Hyprland sets the window's size itself. Needed because a window that has just been carried onto the
// desktop being looked at is not let to redraw until the next desktop switch, so asking the app to
// resize (which takes a redrawn frame) would sit waiting. The app is asked too, so it agrees.
QString Desktop::resizeCommand(const QString &address, int width, int height) {
    return QStringLiteral("hl.dsp.window.resize({ x = %1, y = %2, relative = false, window = \"address:%3\" })").arg(width).arg(height).arg(address);
}

QString Desktop::moveCommand(const QString &address, int x, int y) {
    return QStringLiteral("hl.dsp.window.move({ x = %1, y = %2, window = \"address:%3\" })").arg(x).arg(y).arg(address);
}

// ---- talking to Hyprland ----------------------------------------------------------------------

void Desktop::connectEvents() {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
    m_events.abort();
    m_events.connectToServer(runtime + QStringLiteral("/hypr/") + signature + QStringLiteral("/.socket2.sock"));
    // Where we are now, so the first switch is judged right.
    runHyprctl({QStringLiteral("-j"), QStringLiteral("activeworkspace")}, [this](const QByteArray &out) {
        const int id = QJsonDocument::fromJson(out).object().value(QStringLiteral("id")).toInt();
        if (id > 0)
            m_active = id;
    });
}

void Desktop::readEvents() {
    m_buffer += m_events.readAll();
    int end;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        const int id = workspaceFromEvent(line);
        if (id > 0) {
            qInfo("desktop: switched to desktop %d", id);
            m_active = id;
            m_debounce.start();
        }
    }
    if (m_buffer.size() > 65536)
        m_buffer.clear();
}

void Desktop::runHyprctl(const QStringList &args, std::function<void(const QByteArray &)> done) {
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [process, done] {
        if (done)
            done(process->readAllStandardOutput());
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            process->deleteLater();
    });
    process->setStandardErrorFile(QProcess::nullDevice());
    process->start(QStringLiteral("hyprctl"), args);
}

void Desktop::dispatch(const QString &lua) {
    runHyprctl({QStringLiteral("dispatch"), lua});
}

void Desktop::withOwnClient(std::function<void(const QJsonObject &)> done) {
    if (!m_available)
        return;
    runHyprctl({QStringLiteral("-j"), QStringLiteral("clients")}, [done](const QByteArray &out) {
        const qint64 pid = QCoreApplication::applicationPid();
        for (const auto &value : QJsonDocument::fromJson(out).array()) {
            const QJsonObject client = value.toObject();
            if (client.value(QStringLiteral("pid")).toInteger() == pid && client.value(QStringLiteral("mapped")).toBool(true)) {
                done(client);
                return;
            }
        }
    });
}

void Desktop::rememberPlace() {
    withOwnClient([this](const QJsonObject &mine) {
        const QJsonArray at = mine.value(QStringLiteral("at")).toArray();
        m_peekX = at.at(0).toInt();
        m_peekY = at.at(1).toInt();
        m_hasPeek = true;
    });
}

// The window is changing size and Hyprland may move it while it does: look a few times, and each time pull it back
// inside the screen if it has strayed.
void Desktop::keepOnScreen() {
    keepOnScreenStep(0);
}

void Desktop::keepOnScreenStep(int attempt) {
    if (!m_available || attempt > 8)
        return;
    withOwnClient([this, attempt](const QJsonObject &mine) {
        const QString address = mine.value(QStringLiteral("address")).toString();
        const int monitorId = mine.value(QStringLiteral("monitor")).toInt(-1);
        const QJsonArray at = mine.value(QStringLiteral("at")).toArray();
        const QJsonArray size = mine.value(QStringLiteral("size")).toArray();
        runHyprctl({QStringLiteral("-j"), QStringLiteral("monitors")}, [=](const QByteArray &out) {
            QJsonObject monitor;
            for (const auto &value : QJsonDocument::fromJson(out).array())
                if (value.toObject().value(QStringLiteral("id")).toInt() == monitorId)
                    monitor = value.toObject();
            if (!monitor.isEmpty()) {
                const auto fit = clampInside(monitor, at.at(0).toInt(), at.at(1).toInt(), size.at(0).toInt(), size.at(1).toInt());
                if (fit.first != at.at(0).toInt() || fit.second != at.at(1).toInt())
                    dispatch(moveCommand(address, fit.first, fit.second));
            }
            QTimer::singleShot(120, this, [this, attempt] { keepOnScreenStep(attempt + 1); });
        });
    });
}

// Small again: back to the spot it was in before it stepped up.
void Desktop::restorePlace() {
    if (!m_hasPeek)
        return;
    withOwnClient([this](const QJsonObject &mine) {
        placeWhenSized(mine.value(QStringLiteral("address")).toString(), 1, m_peekX, m_peekY);
    });
}

// Looks at where this window is now, and decides.
void Desktop::evaluate() {
    if (!m_available || m_evaluating)
        return;
    m_evaluating = true;
    runHyprctl({QStringLiteral("-j"), QStringLiteral("clients")}, [this](const QByteArray &out) {
        m_evaluating = false;
        QJsonObject mine;
        const qint64 pid = QCoreApplication::applicationPid();
        for (const auto &value : QJsonDocument::fromJson(out).array()) {
            const QJsonObject client = value.toObject();
            if (client.value(QStringLiteral("pid")).toInteger() == pid && client.value(QStringLiteral("mapped")).toBool(true)) {
                mine = client;
                break;
            }
        }
        if (mine.isEmpty())
            return;
        const QJsonObject workspace = mine.value(QStringLiteral("workspace")).toObject();
        State state;
        state.follow = m_follow;
        state.playing = m_playing;
        state.awayByUs = m_away;
        state.pinned = mine.value(QStringLiteral("pinned")).toBool();
        state.hidden = mine.value(QStringLiteral("hidden")).toBool();
        state.special = workspace.value(QStringLiteral("name")).toString().startsWith(QLatin1String("special"));
        state.activeWorkspace = m_active;
        state.clientWorkspace = workspace.value(QStringLiteral("id")).toInt();
        // While not carried along, wherever the window is, that is home (you may have moved it).
        if (!m_away)
            m_home = state.clientWorkspace;
        state.home = m_home;
        const Step step = decide(state);
        qInfo("desktop: step %d (away=%d active=%d home=%d window on %d pinned=%d playing=%d)", int(step), m_away, m_active, m_home,
              state.clientWorkspace, state.pinned, m_playing);
        apply(step, mine);
    });
}

void Desktop::apply(Step step, const QJsonObject &client) {
    if (client.value(QStringLiteral("address")).toString().isEmpty())
        return;
    if (step == Step::GoAway)
        goAway(client);
    else if (step == Step::ComeHome)
        comeHome(client);
    else if (step == Step::KeepAway)
        noteUserMove(client); // parked on another desktop: remember if the listener has dragged it somewhere
}

// If the mini player is not where it was put, the listener moved it: that is where it belongs now,
// on every desktop and the next time it is carried along.
void Desktop::noteUserMove(const QJsonObject &client) {
    if (!m_away)
        return;
    const QJsonArray at = client.value(QStringLiteral("at")).toArray();
    const QJsonArray size = client.value(QStringLiteral("size")).toArray();
    if (at.size() < 2 || size.size() < 2 || size.at(1).toInt() > 200) // only while it is the mini player
        return;
    const int x = at.at(0).toInt(), y = at.at(1).toInt();
    if (qAbs(x - m_placedX) <= 3 && qAbs(y - m_placedY) <= 3)
        return;
    m_placedX = x;
    m_placedY = y;
    m_hasPark = true;
    m_parkX = x - m_originX;
    m_parkY = y - m_originY;
    if (!m_prefsPath.isEmpty()) {
        QSettings prefs(m_prefsPath, QSettings::IniFormat);
        prefs.setValue(QStringLiteral("desktop/parkX"), m_parkX);
        prefs.setValue(QStringLiteral("desktop/parkY"), m_parkY);
    }
}

void Desktop::simulate(bool away) {
    if (!m_available)
        return;
    runHyprctl({QStringLiteral("-j"), QStringLiteral("clients")}, [this, away](const QByteArray &out) {
        const qint64 pid = QCoreApplication::applicationPid();
        for (const auto &value : QJsonDocument::fromJson(out).array()) {
            const QJsonObject client = value.toObject();
            if (client.value(QStringLiteral("pid")).toInteger() != pid)
                continue;
            if (away && !m_away)
                goAway(client);
            else if (!away && m_away)
                comeHome(client);
            return;
        }
    });
}

// Carried along: pinned first (so the window is on the screen being looked at and can draw, which a
// window on a desktop you have left cannot), then made small, and only then parked in the corner.
void Desktop::goAway(const QJsonObject &client) {
    const QString address = client.value(QStringLiteral("address")).toString();
    const QJsonArray at = client.value(QStringLiteral("at")).toArray();
    m_savedX = at.at(0).toInt();
    m_savedY = at.at(1).toInt();
    m_address = address;
    // Which monitor are we on? The one showing the desktop we just switched to.
    runHyprctl({QStringLiteral("-j"), QStringLiteral("monitors")}, [this, address](const QByteArray &out) {
        QJsonObject monitor;
        const QJsonArray monitors = QJsonDocument::fromJson(out).array();
        for (const auto &value : monitors) {
            const QJsonObject candidate = value.toObject();
            if (candidate.value(QStringLiteral("activeWorkspace")).toObject().value(QStringLiteral("id")).toInt() == m_active)
                monitor = candidate;
        }
        if (monitor.isEmpty() && !monitors.isEmpty())
            monitor = monitors.first().toObject();
        if (monitor.isEmpty())
            return;
        const auto origin = topLeft(monitor, 0);
        m_originX = origin.first;
        m_originY = origin.second;
        const auto corner = parkSpot(monitor, kMargin, m_hasPark, m_parkX, m_parkY);
        m_placedX = corner.first;
        m_placedY = corner.second;
        m_autoMini = !m_mini;
        m_expectedMini = true;
        setAway(true); // first, so the window knows this mini state is the desktop's doing, not yours
        qInfo("desktop: parking at %d,%d", corner.first, corner.second);
        dispatch(pinCommand(address, true));
        // A moment later, once it is on this screen and can draw, shrink it; then wait for the size.
        QTimer::singleShot(150, this, [this, address, corner] {
            qInfo("desktop: asking for the mini player");
            if (m_autoMini) {
                dispatch(resizeCommand(address, kWidth, kMiniHeight));
                emit miniRequested(true);
            }
            placeWhenSized(address, m_autoMini ? 1 : 0, corner.first, corner.second);
        });
    });
}

// Home again: unpinned where it is (this desktop is its own), made full again, then put back where it was.
void Desktop::comeHome(const QJsonObject &client) {
    const QString address = client.value(QStringLiteral("address")).toString();
    noteUserMove(client); // before it is unpinned and grown: where was it left?
    dispatch(pinCommand(address, false));
    const bool grow = m_autoMini && m_mini;
    if (grow) {
        m_expectedMini = false;
        dispatch(resizeCommand(address, kWidth, kFullHeight));
        emit miniRequested(false);
    }
    m_autoMini = false;
    setAway(false);
    placeWhenSized(address, grow ? 2 : 0, m_savedX, m_savedY);
}

void Desktop::placeWhenSized(const QString &address, int size, int x, int y, int attempt, int stable) {
    runHyprctl({QStringLiteral("-j"), QStringLiteral("clients")}, [=](const QByteArray &out) {
        QJsonObject mine;
        for (const auto &value : QJsonDocument::fromJson(out).array())
            if (value.toObject().value(QStringLiteral("address")).toString() == address)
                mine = value.toObject();
        if (mine.isEmpty())
            return;
        const int height = mine.value(QStringLiteral("size")).toArray().at(1).toInt();
        const QJsonArray at = mine.value(QStringLiteral("at")).toArray();
        const bool sized = size == 0 || (size == 1 ? height <= 200 : height >= 400);
        const bool placed = qAbs(at.at(0).toInt() - x) <= 2 && qAbs(at.at(1).toInt() - y) <= 2;
        if (attempt % 5 == 0)
            qInfo("desktop: waiting (attempt %d) size %d placed %d, height now %d", attempt, sized, placed, height);
        if (sized && !placed)
            dispatch(moveCommand(address, x, y));
        // Done once the size and the place have held for two looks in a row (it may still be settling).
        const int next = sized && placed ? stable + 1 : 0;
        if (next >= 2 || attempt >= 45)
            return;
        QTimer::singleShot(100, this, [=] { placeWhenSized(address, size, x, y, attempt + 1, next); });
    });
}
