#include "backend.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QtNumeric>

void Bridge::ingest(const QString &json) {
    emit received(json);
}

Backend::Backend(bool preview, QObject *parent)
    : QObject(parent), m_bridge(new Bridge(this)), m_preview(preview) {
    m_dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (m_dataPath.isEmpty())
        m_dataPath = QDir::homePath() + "/.local/share/click";
    m_shotPath = qEnvironmentVariable("CLICK_SHOT");
    connect(m_bridge, &Bridge::received, this, &Backend::ingest);

    // The progress bar keeps moving between the page's reports.
    auto *clock = new QTimer(this);
    clock->setInterval(100);
    connect(clock, &QTimer::timeout, this, [this] {
        if (!m_playing)
            return;
        m_position += 0.1;
        if (m_duration > 0 && m_position > m_duration)
            m_position = m_duration;
        emit playbackChanged();
    });
    clock->start();

    if (m_preview) {
        m_signedIn = true;
        m_playing = true;
        m_track = QStringLiteral("Night Drive");
        m_artist = QStringLiteral("The Midnight");
        m_album = QStringLiteral("Endless Summer");
        m_position = 83;
        m_duration = 214;
        m_volume = 0.7;
        Screen songs;
        songs.id = QStringLiteral("songs");
        songs.kind = QStringLiteral("list");
        songs.title = tr("Songs");
        const QStringList names{QStringLiteral("Night Drive"), QStringLiteral("Sunset"), QStringLiteral("Days of Thunder"),
                                QStringLiteral("Los Angeles"), QStringLiteral("Crockett's Theme"), QStringLiteral("Vampires")};
        for (const auto &name : names)
            songs.rows.append(Row{name, QStringLiteral("The Midnight"), QStringLiteral("play"), QStringLiteral("songs"), name});
        songs.selected = 0;
        m_stack = {songs};
        if (qEnvironmentVariableIsSet("CLICK_PREVIEW_NOW"))
            showNowPlaying();
        return;
    }
    rebuildRoot();
}

QString Backend::formatTime(double seconds) {
    int whole = seconds < 0 ? 0 : int(seconds + 0.5);
    const int hours = whole / 3600;
    whole %= 3600;
    const int minutes = whole / 60;
    const int secs = whole % 60;
    if (hours > 0)
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
}

QString Backend::jsString(const QString &text) {
    QString out = QStringLiteral("\"");
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case '\\': out += QStringLiteral("\\\\"); break;
        case '"': out += QStringLiteral("\\\""); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        default: out += ch; break;
        }
    }
    out += QLatin1Char('"');
    return out;
}

QString Backend::title() const {
    return m_stack.isEmpty() ? QString() : m_stack.last().title;
}

QString Backend::kind() const {
    return m_stack.isEmpty() ? QStringLiteral("list") : m_stack.last().kind;
}

int Backend::selected() const {
    return m_stack.isEmpty() ? 0 : m_stack.last().selected;
}

QVariantList Backend::rowsOf(const Screen &screen) const {
    QVariantList list;
    for (const auto &row : screen.rows) {
        list.append(QVariantMap{{QStringLiteral("label"), row.label},
                                {QStringLiteral("detail"), row.detail},
                                {QStringLiteral("action"), row.action},
                                {QStringLiteral("type"), row.type},
                                {QStringLiteral("appleId"), row.id}});
    }
    return list;
}

QVariantList Backend::rows() const {
    return m_stack.isEmpty() ? QVariantList{} : rowsOf(m_stack.last());
}

void Backend::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit sessionChanged();
}

Backend::Screen Backend::rootScreen() const {
    Screen screen;
    screen.id = QStringLiteral("root");
    screen.kind = QStringLiteral("list");
    screen.title = QStringLiteral("Click");
    if (!m_signedIn) {
        screen.rows.append(Row{tr("Sign In"), {}, QStringLiteral("signin"), {}, {}});
        return screen;
    }
    screen.rows.append(Row{tr("Music"), {}, QStringLiteral("music"), {}, {}});
    screen.rows.append(Row{tr("Search"), {}, QStringLiteral("search"), {}, {}});
    if (!m_track.isEmpty())
        screen.rows.append(Row{tr("Now Playing"), m_track, QStringLiteral("now"), {}, {}});
    return screen;
}

Backend::Screen Backend::musicScreen() const {
    Screen screen;
    screen.id = QStringLiteral("music");
    screen.kind = QStringLiteral("list");
    screen.title = tr("Music");
    const QList<Row> rows{{tr("Playlists"), {}, QStringLiteral("library"), {}, QStringLiteral("/v1/me/library/playlists")},
                          {tr("Artists"), {}, QStringLiteral("library"), {}, QStringLiteral("/v1/me/library/artists")},
                          {tr("Albums"), {}, QStringLiteral("library"), {}, QStringLiteral("/v1/me/library/albums")},
                          {tr("Songs"), {}, QStringLiteral("library"), {}, QStringLiteral("/v1/me/library/songs")},
                          {tr("Recently Played"), {}, QStringLiteral("library"), {}, QStringLiteral("/v1/me/recent/played/tracks")}};
    for (const auto &row : rows)
        screen.rows.append(row);
    return screen;
}

void Backend::rebuildRoot() {
    const int selected = m_stack.isEmpty() || m_stack.first().id != QStringLiteral("root") ? 0 : m_stack.first().selected;
    Screen root = rootScreen();
    root.selected = qMin(selected, qMax(0, root.rows.size() - 1));
    if (m_stack.isEmpty())
        m_stack.append(root);
    else if (m_stack.first().id == QStringLiteral("root"))
        m_stack.first() = root;
    if (!m_signedIn)
        m_notice = tr("Your sign-in stays on this machine.");
    else if (m_notice == tr("Your sign-in stays on this machine."))
        m_notice.clear();
    if (m_status.isEmpty() || m_status == tr("Your sign-in stays on this machine."))
        setStatus(m_notice);
    emit screenChanged();
}

void Backend::retranslate() {
    for (auto &screen : m_stack) {
        if (screen.id == QStringLiteral("root"))
            screen = rootScreen();
        else if (screen.id == QStringLiteral("music"))
            screen = musicScreen();
        else if (screen.id == QStringLiteral("now"))
            screen.title = tr("Now Playing");
    }
    emit screenChanged();
}

void Backend::push(Screen screen) {
    m_stack.append(screen);
    emit screenChanged();
}

void Backend::queue(const QString &command, const QString &arg, const QString &type) {
    m_activeToken = ++m_serial;
    sendCommand(command, arg, type, m_activeToken);
}

void Backend::sendCommand(const QString &command, const QString &arg, const QString &type, int token) {
    // The page's installed hook runs the command and reports back through the bridge.
    m_command = QStringLiteral("window.__podExec&&window.__podExec(%1,%2,%3,%4)")
                    .arg(jsString(command), jsString(arg), jsString(type))
                    .arg(token);
    // The page script may not be installed yet. It picks this up when it loads.
    if (command == QStringLiteral("authorize"))
        m_command.prepend(QStringLiteral("window.__podWantAuth=1;"));
    emit commandReady();
}

void Backend::showToast(const QString &text) {
    m_toast = text;
    emit toastChanged();
    QTimer::singleShot(1800, this, [this, text] {
        if (m_toast != text)
            return;
        m_toast.clear();
        emit toastChanged();
    });
}

void Backend::openQueue() {
    if (!m_signedIn || m_signingIn)
        return;
    if (!m_stack.isEmpty() && m_stack.last().id == QStringLiteral("queue"))
        return;
    Screen up;
    up.id = QStringLiteral("queue");
    up.kind = QStringLiteral("list");
    up.title = tr("Up Next");
    push(up);
    setStatus(tr("Loading…"));
    queue(QStringLiteral("upnext"));
}

// Songs, albums and playlists can be queued; artists and menu rows cannot.
void Backend::addToQueue(int index, bool playNext) {
    if (m_stack.isEmpty())
        return;
    const Screen &screen = m_stack.last();
    if (index < 0 || index >= screen.rows.size())
        return;
    const Row &row = screen.rows.at(index);
    if (row.action != QStringLiteral("play"))
        return;
    sendCommand(playNext ? QStringLiteral("playnext") : QStringLiteral("playlater"), row.id, row.type, 0);
    showToast(playNext ? tr("Playing next") : tr("Added to Up Next"));
}

void Backend::showNowPlaying() {
    if (!m_stack.isEmpty() && m_stack.last().kind == QStringLiteral("now"))
        return;
    Screen now;
    now.id = QStringLiteral("now");
    now.kind = QStringLiteral("now");
    now.title = tr("Now Playing");
    push(now);
}

void Backend::start() {
    // Qt WebEngine looks for the CDM beside the Chromium config, while Chrome
    // keeps it one directory deeper. Link it into the path Qt searches.
    const QString bundled = QStringLiteral("/opt/google/chrome/WidevineCdm/_platform_specific/linux_x64/libwidevinecdm.so");
    const QString installed = QDir::homePath() + QStringLiteral("/.config/chromium/WidevineCdm/4.10.3112.0/_platform_specific/linux_x64/libwidevinecdm.so");
    const QString real = QFile::exists(bundled) ? bundled : installed;
    const QString linkDir = QDir::homePath() + QStringLiteral("/.config/chromium/WidevineCdm");
    const QString link = linkDir + QStringLiteral("/libwidevinecdm.so");
    if (!QFile::exists(real)) {
        setStatus(tr("Widevine is missing, so Apple Music cannot play."));
    } else if (!QFile::exists(link)) {
        QDir().mkpath(linkDir);
        if (!QFile::link(real, link) && !QFile::exists(link))
            setStatus(tr("Widevine is missing, so Apple Music cannot play."));
    }
}

void Backend::wheel(int steps) {
    if (steps == 0 || m_stack.isEmpty())
        return;
    Screen &screen = m_stack.last();
    if (screen.kind == QStringLiteral("now")) {
        m_position = qMax(0.0, m_position + steps * 2.0);
        if (m_duration > 0)
            m_position = qMin(m_position, m_duration);
        emit playbackChanged();
        queue(QStringLiteral("seek"), QString::number(m_position));
        return;
    }
    if (screen.rows.isEmpty())
        return;
    int index = screen.selected + steps;
    const int count = screen.rows.size();
    index %= count;
    if (index < 0)
        index += count;
    if (index == screen.selected)
        return;
    screen.selected = index;
    emit screenChanged();
}

void Backend::center() {
    if (m_stack.isEmpty())
        return;
    const Screen &screen = m_stack.last();
    if (screen.kind == QStringLiteral("now")) {
        playPause();
        return;
    }
    if (screen.kind == QStringLiteral("search")) {
        const QString term = screen.id.mid(QStringLiteral("search:").size());
        if (screen.rows.isEmpty()) {
            if (!term.trimmed().isEmpty())
                queue(QStringLiteral("search"), term.trimmed());
            return;
        }
    }
    if (screen.selected < 0 || screen.selected >= screen.rows.size())
        return;
    const Row row = screen.rows.at(screen.selected);
    if (row.action == QStringLiteral("signin")) {
        if (m_signingIn)
            return;
        m_signingIn = true;
        emit sessionChanged();
        queue(QStringLiteral("authorize"));
        return;
    }
    if (row.action == QStringLiteral("music")) {
        push(musicScreen());
        return;
    }
    if (row.action == QStringLiteral("search")) {
        Screen search;
        search.id = QStringLiteral("search:");
        search.kind = QStringLiteral("search");
        search.title = tr("Search");
        push(search);
        return;
    }
    if (row.action == QStringLiteral("now")) {
        showNowPlaying();
        return;
    }
    if (row.action == QStringLiteral("library")) {
        Screen list;
        list.id = QStringLiteral("library:") + row.id;
        list.kind = QStringLiteral("list");
        list.title = row.label;
        push(list);
        setStatus(tr("Loading…"));
        queue(QStringLiteral("library"), row.id);
        return;
    }
    if (row.action == QStringLiteral("open")) {
        Screen list;
        list.id = QStringLiteral("open:") + row.id;
        list.kind = QStringLiteral("list");
        list.title = row.label;
        push(list);
        setStatus(tr("Loading…"));
        queue(QStringLiteral("library"), row.id, row.type);
        return;
    }
    if (row.action == QStringLiteral("jump")) {
        sendCommand(QStringLiteral("jump"), row.id, {}, 0);
        showNowPlaying();
        return;
    }
    if (row.action == QStringLiteral("play")) {
        queue(QStringLiteral("play"), row.id, row.type);
        showNowPlaying();
    }
}

void Backend::menu() {
    m_activeToken = 0;
    if (m_signingIn) {
        m_signingIn = false;
        emit sessionChanged();
        return;
    }
    if (m_stack.size() <= 1)
        return;
    m_stack.removeLast();
    if (m_status == tr("Loading…"))
        setStatus(m_notice);
    emit screenChanged();
}

void Backend::cancelSignIn() {
    if (!m_signingIn)
        return;
    m_signingIn = false;
    emit sessionChanged();
}

void Backend::playPause() {
    if (m_preview) {
        m_playing = !m_playing;
        emit playbackChanged();
        return;
    }
    queue(QStringLiteral("toggle"));
}

void Backend::nextTrack() {
    queue(QStringLiteral("next"));
}

void Backend::previousTrack() {
    if (m_position > 3) {
        m_position = 0;
        emit playbackChanged();
        queue(QStringLiteral("seek"), QStringLiteral("0"));
        return;
    }
    queue(QStringLiteral("prev"));
}

void Backend::nudgeVolume(int steps) {
    setVolume(m_volume + steps * 0.05);
}

void Backend::setVolume(double value) {
    const double next = qBound(0.0, value, 1.0);
    if (qAbs(next - m_volume) < 0.001)
        return;
    m_volume = next;
    emit playbackChanged();
    emit volumeNudged();
    queue(QStringLiteral("volume"), QString::number(m_volume, 'f', 3));
}

void Backend::seek(double seconds) {
    seconds = m_duration > 0 ? qBound(0.0, seconds, m_duration) : qMax(0.0, seconds);
    m_position = seconds;
    emit playbackChanged();
    if (!m_preview)
        queue(QStringLiteral("seek"), QString::number(m_position));
}

void Backend::openSearch() {
    if (!m_signedIn || m_signingIn)
        return;
    if (!m_stack.isEmpty() && m_stack.last().kind == QStringLiteral("search"))
        return;
    Screen search;
    search.id = QStringLiteral("search:");
    search.kind = QStringLiteral("search");
    search.title = tr("Search");
    push(search);
}

// Back one screen; at the top of the menus, return to the song if there is one.
void Backend::back() {
    if (m_stack.size() > 1)
        menu();
    else if (!m_track.isEmpty())
        showNowPlaying();
}

void Backend::openPlayer() {
    showNowPlaying();
}

void Backend::setSearch(const QString &text) {
    if (m_stack.isEmpty() || m_stack.last().kind != QStringLiteral("search"))
        return;
    Screen &screen = m_stack.last();
    screen.id = QStringLiteral("search:") + text;
    screen.rows.clear();
    screen.selected = 0;
    emit screenChanged();
    if (text.trimmed().size() < 2)
        return;
    setStatus(tr("Loading…"));
    queue(QStringLiteral("search"), text.trimmed());
}

void Backend::ingest(const QString &json) {
    const QJsonObject object = QJsonDocument::fromJson(json.toUtf8()).object();
    if (object.isEmpty())
        return;
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("rows")) {
        const int token = object.value(QStringLiteral("token")).toInt();
        if (token != m_activeToken || m_stack.isEmpty())
            return;
        QVector<Row> rows;
        const QJsonArray list = object.value(QStringLiteral("rows")).toArray();
        for (const auto &value : list) {
            const QJsonObject row = value.toObject();
            const QString id = row.value(QStringLiteral("id")).toString();
            const QString rowType = row.value(QStringLiteral("type")).toString();
            const QString label = row.value(QStringLiteral("label")).toString();
            if (id.isEmpty() || label.isEmpty())
                continue;
            QString action = QStringLiteral("play");
            QString path = id;
            if (rowType == QStringLiteral("queueitem")) {
                action = QStringLiteral("jump");
            } else if (rowType.contains(QStringLiteral("artist"))) {
                action = QStringLiteral("open");
                path = QStringLiteral("/v1/me/library/artists/") + id + QStringLiteral("/albums");
            }
            rows.append(Row{label, row.value(QStringLiteral("detail")).toString(), action, rowType, path});
        }
        m_stack.last().rows = rows;
        m_stack.last().selected = 0;
        setStatus(rows.isEmpty() ? tr("Nothing here") : QString());
        emit screenChanged();
        return;
    }

    if (type != QStringLiteral("state"))
        return;

    const bool wasSignedIn = m_signedIn;
    const bool wasPlaying = m_playing;
    bool playback = false;
    auto takeBool = [&](const char *key, bool &field) {
        const QJsonValue value = object.value(QLatin1String(key));
        if (!value.isBool() || field == value.toBool())
            return;
        field = value.toBool();
        playback = true;
    };
    auto takeString = [&](const char *key, QString &field) {
        if (!object.contains(QLatin1String(key)))
            return;
        const QString value = object.value(QLatin1String(key)).toString();
        if (field == value)
            return;
        field = value;
        playback = true;
    };
    auto takeDouble = [&](const char *key, double &field) {
        const QJsonValue value = object.value(QLatin1String(key));
        if (!value.isDouble() || qFuzzyCompare(field + 1, value.toDouble() + 1))
            return;
        field = value.toDouble();
        playback = true;
    };
    if (object.contains(QStringLiteral("authorized")))
        takeBool("authorized", m_signedIn);
    takeBool("playing", m_playing);
    const QString previousTitle = m_track;
    if (object.contains(QStringLiteral("title"))) {
        const QString value = object.value(QStringLiteral("title")).toString();
        // A blank poll while audio is already going must not wipe the song we have.
        const bool keep = value.isEmpty() && !m_track.isEmpty() && (m_playing || wasPlaying);
        if (!keep && m_track != value) {
            m_track = value;
            playback = true;
        }
    }
    takeString("artist", m_artist);
    takeString("album", m_album);
    takeString("artwork", m_artwork);
    takeString("trackId", m_trackId);
    if (object.contains(QStringLiteral("position")) && object.value(QStringLiteral("position")).isDouble()) {
        const double value = object.value(QStringLiteral("position")).toDouble();
        const bool sameSong = m_track == previousTitle;
        const bool bogusZero = sameSong && value <= 0.05 && m_playing && m_position > 1.5 && m_duration > 5.0;
        if (!bogusZero && !qFuzzyCompare(m_position + 1.0, value + 1.0)) {
            m_position = value;
            playback = true;
        }
    }
    takeDouble("duration", m_duration);
    takeDouble("volume", m_volume);
    if (object.contains(QStringLiteral("error"))) {
        const QString error = object.value(QStringLiteral("error")).toString();
        setStatus(error == QStringLiteral("Apple Music did not load.") ? tr("Apple Music did not load.") : error);
    }
    else if (m_signedIn && m_status == tr("Your sign-in stays on this machine."))
        setStatus({});
    if (m_signedIn && m_signingIn) {
        m_signingIn = false;
        emit sessionChanged();
    }
    if (wasSignedIn != m_signedIn) {
        playback = true;
        rebuildRoot();
    } else if (m_signedIn && m_track != previousTitle && !m_stack.isEmpty() && m_stack.first().id == QStringLiteral("root")) {
        Screen root = rootScreen();
        root.selected = qMin(m_stack.first().selected, qMax(0, root.rows.size() - 1));
        m_stack.first() = root;
        emit screenChanged();
    }
    // Open the player when a song starts and the menu is sitting on the root.
    // Browsing a library list (a deeper screen) is left alone.
    const bool titleArrived = !m_track.isEmpty() && m_track != previousTitle;
    const bool started = m_playing && !wasPlaying;
    if (m_signedIn && m_stack.size() == 1 && (titleArrived || started))
        showNowPlaying();
    if (playback)
        emit playbackChanged();
}
