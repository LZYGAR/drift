#include "backend.h"
#include "mixer.h"

#include <QCryptographicHash>
#include <QTime>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QtNumeric>
#include <QUrl>
#include <cmath>

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

    m_mixer = new Mixer(this);
    // Songs show up one by one while Grok is still writing its answer.
    connect(m_mixer, &Mixer::partial, this, [this](const QString &tail, const QVariantList &songs) {
        m_mixPreview = tail;
        if (songs.size() > m_mixSongs.size())
            m_mixSongs = songs;
        // The first song is enough to start the music; the rest are found while it plays.
        if (!m_mixFirstSent && !m_preview && m_mixBusy && !m_mixSongs.isEmpty()) {
            const QVariantList usable = withoutAvoided(m_mixSongs);
            if (!usable.isEmpty()) {
                m_mixFirstSent = m_mixFirstPending = true;
                m_mixFirst = usable.first().toMap();
                m_mixStatus = tr("Starting with “%1”…").arg(m_mixFirst.value(QStringLiteral("title")).toString());
                sendCommand(QStringLiteral("mix"),
                            QString::fromUtf8(QJsonDocument(QJsonArray{QJsonObject::fromVariantMap(m_mixFirst)}).toJson(QJsonDocument::Compact)),
                            QStringLiteral("first"), 0);
            }
        }
        emit mixChanged();
    });
    connect(m_mixer, &Mixer::finished, this, [this](const QString &name, const QVariantList &asked) {
        const QVariantList songs = withoutAvoided(asked);
        m_mixName = name;
        m_mixSongs = songs;
        m_mixPreview.clear();
        m_mixFinished = true;
        if (m_mixFirstSent) {
            // The first song is already playing: find the others and queue them behind it.
            m_mixRest.clear();
            const QString firstKey = songKey(m_mixFirst.value(QStringLiteral("title")).toString(), m_mixFirst.value(QStringLiteral("artist")).toString());
            for (const auto &value : songs) {
                const QVariantMap song = value.toMap();
                if (songKey(song.value(QStringLiteral("title")).toString(), song.value(QStringLiteral("artist")).toString()) != firstKey)
                    m_mixRest.append(song);
            }
            m_mixStatus = tr("Playing — finding %1 more songs…").arg(m_mixRest.size());
            emit mixChanged();
            if (!m_mixFirstPending)
                sendMixRest();
            return;
        }
        m_mixStatus = tr("Finding %1 songs on Apple Music…").arg(songs.size());
        emit mixChanged();
        // The page looks each song up and queues what it finds.
        sendCommand(QStringLiteral("mix"), QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(songs)).toJson(QJsonDocument::Compact)),
                    name, 0);
    });
    connect(m_mixer, &Mixer::failed, this, [this](const QString &message) {
        m_mixBusy = false;
        m_mixFirstSent = m_mixFirstPending = false;
        m_mixPreview.clear();
        m_mixStatus = message;
        emit mixChanged();
    });

    // Similar songs: a second helper, so a mix and a suggestion never wait on each other.
    m_similar = new Mixer(this);
    m_similarTimer.setSingleShot(true);
    m_similarTimer.setInterval(8000); // long enough that skipping through songs asks for nothing
    connect(&m_similarTimer, &QTimer::timeout, this, &Backend::askForSimilar);
    connect(m_similar, &Mixer::finished, this, [this](const QString &, const QVariantList &songs) {
        // Dropped if the song changed, or the setting was turned off, while Grok was thinking.
        if (!m_autoSimilar || !m_chainOn) {
            qInfo("similar: answer dropped (setting turned off, or something else started playing)");
            m_similarAsked = false;
            setSimilarStatus({});
            return;
        }
        // Keep what has not played or been suggested before, and is not by an artist they avoid.
        QVariantList candidates;
        for (const auto &value : songs) {
            const QVariantMap song = value.toMap();
            const QString title = song.value(QStringLiteral("title")).toString();
            const QString artist = song.value(QStringLiteral("artist")).toString();
            const QString key = songKey(title, artist);
            bool seen = key == songKey(m_track, m_artist);
            for (const QString &old : m_recent)
                seen = seen || songKey(old.section(QLatin1Char('\n'), 0, 0), old.section(QLatin1Char('\n'), 1)) == key;
            if (seen || (m_useTaste && m_taste->avoids(artist)))
                continue;
            candidates.append(song);
        }
        if (candidates.isEmpty()) {
            qInfo("similar: all %d suggestions were already played or avoided", int(songs.size()));
            retrySimilar(tr("Grok only suggested songs you have heard."));
            return;
        }
        m_lastCandidates = candidates;
        const QVariantMap first = candidates.first().toMap();
        qInfo("similar: asking the page to queue one of %d (first: %s)", int(candidates.size()),
              qPrintable(first.value(QStringLiteral("title")).toString()));
        setSimilarStatus(tr("Finding “%1” on Apple Music…").arg(first.value(QStringLiteral("title")).toString()));
        sendCommand(QStringLiteral("similar"),
                    QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("songs"), QJsonArray::fromVariantList(candidates)},
                                                                {QStringLiteral("want"), qMax(1, kQueueTarget - qMax(0, m_upcoming))},
                                                                {QStringLiteral("many"), true}})
                                          .toJson(QJsonDocument::Compact)),
                    {}, 0);
    });
    connect(m_similar, &Mixer::failed, this, [this](const QString &message) {
        qInfo("similar: Grok failed: %s", qPrintable(message));
        retrySimilar(message);
    });
    // Ratings and what they teach (see taste.h): one small file next to the other settings.
    m_lyrics = new Lyrics(this);
    m_taste = new Taste(preview ? QString() : m_dataPath + QStringLiteral("/taste.json"), this);
    connect(m_taste, &Taste::changed, this, &Backend::tasteChanged);
    m_stats = new Stats(preview ? QString() : m_dataPath + QStringLiteral("/stats.json"), this);
    if (!preview) {
        QSettings prefs(m_dataPath + QStringLiteral("/prefs.conf"), QSettings::IniFormat);
        m_autoSimilar = prefs.value(QStringLiteral("autoplay/similar"), true).toBool();
        m_useTaste = prefs.value(QStringLiteral("taste/use"), true).toBool();
        m_history = prefs.value(QStringLiteral("search/history")).toStringList();
    }
    // A search is remembered once the typing settles, so half-typed words do not pile up.
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(1600);
    connect(&m_historyTimer, &QTimer::timeout, this, [this] { rememberSearch(m_historyPending); });

    // The progress bar keeps moving between the page's reports.
    auto *clock = new QTimer(this);
    clock->setInterval(100);
    connect(clock, &QTimer::timeout, this, [this] {
        if (!m_playing)
            return;
        m_position += 0.1;
        if (m_duration > 0 && m_position > m_duration)
            m_position = m_duration;
        if (!m_track.isEmpty()) {
            const QString key = m_trackId.isEmpty() ? songKey(m_track, m_artist) : m_trackId;
            m_stats->listen(key, m_track, m_artist, m_artwork, 0.1);
            m_heard += 0.1;
            // Half a minute of a song is a play.
            if (!m_heardCounted && m_heard >= 30) {
                m_heardCounted = true;
                m_stats->countPlay(key, m_track, m_artist, m_artwork);
            }
        }
        if (m_duration > 5)
            m_furthest = qMax(m_furthest, m_position / m_duration);
        emit playbackChanged();
    });
    clock->start();

    // What was playing is remembered, so the next launch picks up where this one stopped.
    if (!preview) {
        m_sessionTimer.setInterval(5000);
        connect(&m_sessionTimer, &QTimer::timeout, this, &Backend::saveSession);
        m_sessionTimer.start();
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { saveSession(); m_stats->flush(); });
    }

    if (m_preview) {
        m_signedIn = true;
        m_playing = true;
        m_track = QStringLiteral("Night Drive");
        m_artist = QStringLiteral("The Midnight");
        m_album = QStringLiteral("Endless Summer");
        m_position = 83;
        m_duration = 214;
        m_volume = 0.7;
        m_trackId = QStringLiteral("preview-1");
        {
            // A believable week of listening for the Wrapped card.
            struct Fake { const char *title, *artist; int minutes; int plays; };
            const Fake fakes[] = {{"Nightcall", "Kavinsky", 212, 41}, {"Night Drive", "The Midnight", 188, 36}, {"Resonance", "HOME", 160, 33},
                                  {"Sunset", "The Midnight", 121, 24}, {"A Real Hero", "College", 96, 19}};
            const QDateTime now = QDateTime::currentDateTime();
            int i = 0;
            for (const Fake &f : fakes) {
                const QString key = QStringLiteral("fake-%1").arg(i++);
                for (int d = 0; d < 7; ++d)
                    m_stats->listen(key, QString::fromLatin1(f.title), QString::fromLatin1(f.artist), {}, f.minutes * 60.0 / 7.0 * (0.4 + 0.15 * ((d * 3 + i) % 5)),
                                    now.addDays(-d).addSecs(-(d % 3) * 3600));
                for (int p = 0; p < f.plays; ++p)
                    m_stats->countPlay(key, QString::fromLatin1(f.title), QString::fromLatin1(f.artist), {});
            }
        }
        m_lyrics->setText(QStringLiteral(
            "[00:00.00]\n[00:04.20]Headlights bleed across the glass\n[00:08.90]We are chasing what we can't get back\n"
            "[00:13.40]City lights are rolling past\n[00:17.80]Hands on the wheel and the night is ours\n"
            "[00:22.50]Neon rain on the dashboard glow\n[00:27.10]Nobody's counting the miles tonight\n"
            "[00:31.60]Radio low and the world is slow\n[00:36.20]Everything's golden in this light\n"
            "[00:40.80]Drive, drive, we are still alive\n[00:45.30]Under the endless summer sky\n"
            "[00:50.00]Drive, drive, into the night\n[00:54.60]Don't look back, we're doing fine\n"
            "[00:59.00]Streetlamps flicker, one by one\n[01:03.50]Every mile a memory\n"
            "[01:08.00]Chasing the end of the sun\n[01:12.60]Nothing left but you and me\n"
            "[01:17.00]Drive, drive, we are still alive\n[01:21.60]Under the endless summer sky\n"
            "[01:26.00]Drive, drive, into the night\n[01:30.60]Don't look back, we're doing fine\n"));
        m_playlists = QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("p.1")}, {QStringLiteral("name"), QStringLiteral("night drive")}},
                                   QVariantMap{{QStringLiteral("id"), QStringLiteral("p.2")}, {QStringLiteral("name"), QStringLiteral("sub-bass euphoria")}},
                                   QVariantMap{{QStringLiteral("id"), QStringLiteral("p.3")}, {QStringLiteral("name"), QStringLiteral("gym 2026")}}};
        if (qEnvironmentVariable("CLICK_PREVIEW_PANEL") == QStringLiteral("search"))
            QTimer::singleShot(0, this, [this] { openSearch(); });
        m_history = {QStringLiteral("frank ocean"), QStringLiteral("ivy"), QStringLiteral("the midnight")};
        m_currentPlaylistId = QStringLiteral("p.1");
        m_currentPlaylistName = QStringLiteral("night drive");
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
        // Lets screenshots show a real cover: CLICK_PREVIEW_ART=/path/to/cover.png
        if (qEnvironmentVariableIsSet("CLICK_PREVIEW_OUT")) {
            m_signedIn = false;
            m_playing = false;
            m_track.clear();
            m_artist.clear();
            m_album.clear();
        }
        if (qEnvironmentVariableIsSet("CLICK_PREVIEW_PAUSED"))
            m_playing = false;
        const QString previewArt = qEnvironmentVariable("CLICK_PREVIEW_ART");
        if (!previewArt.isEmpty()) {
            m_artwork = QUrl::fromLocalFile(previewArt).toString();
            setPalette(paletteFrom(QImage(previewArt)));
        }
        if (qEnvironmentVariableIsSet("CLICK_PREVIEW_MIX")) {
            m_mixStatus = tr("Finding songs… 7 of 20");
            m_mixBusy = true;
            m_mixPreview = QStringLiteral("{\"title\": \"Resonance\", \"artist\": \"HOME\"}, {\"title\": \"Nightcall\", \"art");
            const QList<QPair<QString, QString>> picks{{QStringLiteral("Midnight City"), QStringLiteral("M83")},
                                                       {QStringLiteral("A Real Hero"), QStringLiteral("College & Electric Youth")},
                                                       {QStringLiteral("Tech Noir"), QStringLiteral("Gunship")},
                                                       {QStringLiteral("Resonance"), QStringLiteral("HOME")}};
            for (const auto &pick : picks)
                m_mixSongs.append(QVariantMap{{QStringLiteral("title"), pick.first}, {QStringLiteral("artist"), pick.second}});
            openMix();
        }
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
    screen.title = tr("Music Player");
    if (!m_signedIn) {
        screen.rows.append(Row{tr("Sign In"), {}, QStringLiteral("signin"), {}, {}});
        return screen;
    }
    screen.rows.append(Row{tr("Music"), {}, QStringLiteral("music"), {}, {}});
    screen.rows.append(Row{tr("Search"), {}, QStringLiteral("search"), {}, {}});
    if (mixAvailable())
        screen.rows.append(Row{tr("Make a Mix"), tr("Grok picks the songs"), QStringLiteral("mix"), {}, {}});
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
    if (command == QStringLiteral("autoplay"))
        m_command.prepend(QStringLiteral("window.__podNoAutoplay=%1;").arg(arg == QStringLiteral("1") ? 1 : 0));
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

QColor Backend::accentFrom(const QImage &cover) {
    const auto palette = paletteFrom(cover);
    return palette.isEmpty() ? QColor() : palette.first();
}

// Picks the most vivid hue on the cover, then brings it to a lightness that
// reads well on the dark player. A second hue and a dark tint come with it.
QVector<QColor> Backend::paletteFrom(const QImage &cover) {
    if (cover.isNull())
        return {};
    const QImage small = cover.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    constexpr int bins = 12;
    double weight[bins] = {};
    double red[bins] = {}, green[bins] = {}, blue[bins] = {};
    double total = 0;
    double meanRed = 0, meanGreen = 0, meanBlue = 0;
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const QColor c = small.pixelColor(x, y);
            meanRed += c.redF();
            meanGreen += c.greenF();
            meanBlue += c.blueF();
            const double s = c.hsvSaturationF();
            const double v = c.valueF();
            if (c.hsvHue() < 0 || v < 0.18)
                continue;
            const double w = s * s * v;
            const int bin = qBound(0, c.hsvHue() * bins / 360, bins - 1);
            weight[bin] += w;
            red[bin] += w * c.redF();
            green[bin] += w * c.greenF();
            blue[bin] += w * c.blueF();
            total += w;
        }
    }
    int best = 0;
    for (int i = 1; i < bins; ++i)
        if (weight[i] > weight[best])
            best = i;
    const double pixels = double(small.width()) * small.height();
    const QColor mean = QColor::fromRgbF(meanRed / pixels, meanGreen / pixels, meanBlue / pixels);
    // Little real colour: a white, grey or black cover. The app takes on that cover's own tone (silver for a
    // white one, charcoal for a dark one) instead of borrowing the theme's, or a stray tan from a face.
    if (total < 0.12 * pixels || weight[best] < 0.05 * pixels) {
        const double hue = mean.hslHue() < 0 ? 0.0 : mean.hslHueF();
        const double tint = qMin(0.14, mean.hslSaturationF());
        const double light = mean.lightnessF();
        const QColor accent = QColor::fromHslF(hue, tint, qBound(0.56, light, 0.88));
        const QColor secondary = QColor::fromHslF(hue, tint, qBound(0.36, light - 0.22, 0.66));
        const QColor ambient = QColor::fromHslF(hue, tint, qBound(0.10, light * 0.38, 0.30));
        return {accent, secondary, ambient};
    }
    // The more colourful the cover, the more of it the app wears; a pastel cover stays soft.
    const double vivid = qMin(1.0, total / (0.4 * pixels));
    const double minSat = 0.3 + 0.25 * vivid;
    QColor accent = QColor::fromRgbF(red[best] / weight[best], green[best] / weight[best], blue[best] / weight[best]);
    accent = QColor::fromHslF(accent.hslHueF(), qMax(minSat, accent.hslSaturationF()),
                              qBound(0.58, accent.lightnessF(), 0.70));

    // The second colour is another strong hue at least 60 degrees away. A cover
    // with only one hue gets a neighbour of the accent, so the backdrop still moves.
    int second = -1;
    for (int i = 0; i < bins; ++i) {
        const int apart = qMin(qAbs(i - best), bins - qAbs(i - best));
        if (apart < 2 || weight[i] < weight[best] * 0.15)
            continue;
        if (second < 0 || weight[i] > weight[second])
            second = i;
    }
    QColor secondary;
    if (second >= 0) {
        secondary = QColor::fromRgbF(red[second] / weight[second], green[second] / weight[second],
                                     blue[second] / weight[second]);
        secondary = QColor::fromHslF(secondary.hslHueF(), qMax(0.5, secondary.hslSaturationF()),
                                     qBound(0.42, secondary.lightnessF(), 0.6));
    } else {
        double hue = accent.hslHueF() + 40.0 / 360.0;
        hue -= std::floor(hue);
        secondary = QColor::fromHslF(hue, accent.hslSaturationF() * 0.85, 0.46);
    }

    // The ambient tint is the cover's average, pulled dark so text stays readable.
    const double ambientHue = mean.hslHue() < 0 ? accent.hslHueF() : mean.hslHueF();
    const QColor ambient = QColor::fromHslF(ambientHue, qBound(0.25, mean.hslSaturationF(), 0.6), 0.14);
    return {accent, secondary, ambient};
}

void Backend::setPalette(const QVector<QColor> &palette) {
    const QColor accent = palette.value(0);
    const QColor secondary = palette.value(1);
    const QColor ambient = palette.value(2);
    if (accent == m_artColor && secondary == m_artSecondary && ambient == m_artAmbient)
        return;
    m_artColor = accent;
    m_artSecondary = secondary;
    m_artAmbient = ambient;
    emit artColorChanged();
}

// Keeps one copy of the current cover on disk. Desktop widgets and
// notification daemons read a file far more reliably than an https address.
void Backend::cacheCover(const QByteArray &data, const QString &source) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/covers");
    if (data.isEmpty() || !QDir().mkpath(dir))
        return;
    const QString name = QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    const QString path = dir + QLatin1Char('/') + name + QStringLiteral(".jpg");
    // Only the current cover is kept, so the folder never grows.
    const auto old = QDir(dir).entryList(QDir::Files);
    for (const QString &file : old)
        if (file != name + QStringLiteral(".jpg"))
            QFile::remove(dir + QLatin1Char('/') + file);
    QFile file(path);
    if (!file.exists() && (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()))
        return;
    m_artworkLocal = QUrl::fromLocalFile(path).toString();
    emit playbackChanged();
}

// Fetches the cover from the same CDN the page already uses, only to read its colour.
void Backend::fetchArtColor() {
    if (m_preview)
        return;
    const QString requested = m_artwork;
    const QUrl url(requested);
    m_artworkLocal.clear();
    if (requested.isEmpty() || url.scheme() != QStringLiteral("https")) {
        setPalette({});
        return;
    }
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, requested] {
        reply->deleteLater();
        if (requested != m_artwork || reply->error() != QNetworkReply::NoError)
            return;
        const QByteArray data = reply->read(4 * 1024 * 1024);
        QImage image;
        image.loadFromData(data);
        setPalette(paletteFrom(image));
        if (!image.isNull())
            cacheCover(data, requested);
    });
}

QStringList Backend::mixChips() const {
    return Mixer::contextChips(QDateTime::currentDateTime());
}

bool Backend::similarAvailable() const {
    return m_similar && m_similar->available();
}

void Backend::setMixProgram(const QString &program) {
    m_mixer->setProgram(program);
    emit mixChanged();
}

void Backend::setSimilarProgram(const QString &program) {
    m_similar->setProgram(program);
    emit similarChanged();
}

void Backend::setSimilarStatus(const QString &status) {
    if (status == m_similarStatus)
        return;
    m_similarStatus = status;
    emit similarChanged();
}

void Backend::setAutoSimilar(bool on) {
    if (on == m_autoSimilar)
        return;
    m_autoSimilar = on;
    if (!m_preview) {
        QSettings prefs(m_dataPath + QStringLiteral("/prefs.conf"), QSettings::IniFormat);
        prefs.setValue(QStringLiteral("autoplay/similar"), on);
    }
    m_similarTimer.stop();
    m_autoAdded.clear();
    m_autoAddedIds.clear();
    setSimilarStatus({});
    emit similarChanged();
    showToast(on ? tr("Similar songs on") : tr("Similar songs off"));
    if (!m_preview)
        syncAutoplay();
    // Turning it on in the middle of a song starts the wait for that song.
    if (on && m_playing && !m_track.isEmpty())
        trackChangedForSimilar();
}

// A new song began: wait a while (skipping through songs must not ask Grok about each),
// then see whether it is the last one in the queue.
void Backend::trackChangedForSimilar() {
    m_similarTimer.stop();
    m_similarAsked = false;
    m_similarDone = false;
    m_similarTries = 0;
    m_similarChecks = 0;
    setSimilarStatus({});
    // A song Grok queued carries the chain on; anything else starts over (a solo pick is noticed when asking).
    const bool ours = m_autoAdded.contains(songKey(m_track, m_artist)) || (!m_trackId.isEmpty() && m_autoAddedIds.contains(m_trackId))
                      || m_autoAddedTitles.contains(looseTitle(m_track));
    if (!ours)
        m_chainOn = false;
    if (!m_autoSimilar || m_preview)
        return;
    m_similarTitle = m_track;
    m_similarTimer.start(m_similarDelay);
}

// A title without "(feat. …)", "- Remastered" and the like, lower case: the page may show a song a little
// differently from the catalog entry that was queued.
QString Backend::looseTitle(const QString &title) {
    QString out = title.toLower();
    const int paren = out.indexOf(QLatin1Char('('));
    if (paren > 0)
        out = out.left(paren);
    const int dash = out.indexOf(QStringLiteral(" - "));
    if (dash > 0)
        out = out.left(dash);
    return out.simplified();
}

QString Backend::songKey(const QString &title, const QString &artist) {
    return (title.simplified() + QLatin1Char('\n') + artist.simplified()).toLower();
}

// Look again in a few seconds: the queue may not have settled, or a mix may still be building.
void Backend::waitForSimilar(const QString &why) {
    qInfo("similar: waiting (%s) [upcoming=%d, queue %d at %d, repeat=%d]", qPrintable(why), m_upcoming, m_queueLength,
          m_queuePosition, m_repeat);
    if (++m_similarChecks > 60) { // about four minutes of looking: pause, then look again, for as long as the song plays
        qInfo("similar: pausing for a while, then looking again");
        m_similarChecks = 0;
        m_similarTimer.start(m_similarRecheck > 1000 ? 30000 : m_similarRecheck);
        return;
    }
    m_similarTimer.start(m_similarRecheck);
}

// A suggestion did not work out (Grok failed, or the page could not find it): try again a few times.
void Backend::retrySimilar(const QString &why) {
    m_similarAsked = false;
    for (const auto &value : m_lastCandidates) {
        const QVariantMap song = value.toMap();
        remember(song.value(QStringLiteral("title")).toString(), song.value(QStringLiteral("artist")).toString());
    }
    m_lastCandidates.clear();
    if (!m_autoSimilar || !m_chainOn)
        return;
    if (++m_similarTries > 3) {
        // Not giving up for good: it goes on until the setting is turned off, so wait a little and start again.
        setSimilarStatus(why);
        m_similarTries = 0;
        m_similarTimer.start(m_similarRecheck > 1000 ? 30000 : m_similarRecheck);
        return;
    }
    setSimilarStatus(tr("Trying again…"));
    m_similarTimer.start(m_similarRecheck > 1000 ? 5000 : m_similarRecheck);
}

// The page treats Apple's own autoplay as the queue's tail, which would hide the end of the queue
// from us, so while this feature is on it is switched off (and put back when this is turned off).
void Backend::syncAutoplay() {
    sendCommand(QStringLiteral("autoplay"), m_autoSimilar ? QStringLiteral("1") : QStringLiteral("0"), {}, 0);
}

// Only when this is the last song in the queue, so a playlist or a mix is never interrupted.
void Backend::askForSimilar() {
    if (!m_autoSimilar || m_preview || m_track.isEmpty() || m_track != m_similarTitle || m_similarDone || m_similarAsked)
        return;
    if (!m_playing)
        return waitForSimilar(QStringLiteral("paused"));
    if (m_mixBusy || m_similar->busy())
        return waitForSimilar(QStringLiteral("busy"));
    if (!m_similar->available()) {
        setSimilarStatus(tr("Needs the Grok CLI."));
        return;
    }
    if (m_repeat != 0) {
        setSimilarStatus(tr("Waiting: repeat is on."));
        return waitForSimilar(QStringLiteral("repeat"));
    }
    const bool autoAdded = m_autoAdded.contains(songKey(m_track, m_artist)) || (!m_trackId.isEmpty() && m_autoAddedIds.contains(m_trackId))
                           || m_autoAddedTitles.contains(looseTitle(m_track));
    // A song picked on its own (a queue of one) or one of Grok's own starts the chain. A playlist, album or mix is a
    // list, not a pick, and is left to play (and to end) as it is.
    if (autoAdded || (m_queueLength <= 1 && m_upcoming == 0))
        m_chainOn = true;
    if (!m_chainOn) {
        setSimilarStatus({});
        return waitForSimilar(QStringLiteral("not a pick"));
    }
    // Keep the queue topped up: never more than kQueueTarget songs ahead, never wait for them to play.
    const int need = kQueueTarget - qMax(0, m_upcoming); // unknown counts as empty
    if (need <= 0) {
        setSimilarStatus(tr("%1 songs queued").arg(m_upcoming));
        return waitForSimilar(QStringLiteral("queue is full"));
    }
    remember(m_track, m_artist);
    // More songs "like the last one we queued", so the run drifts along instead of circling the first song.
    const bool fromTail = m_upcoming > 0 && !m_chainTailTitle.isEmpty();
    const QString seedTitle = fromTail ? m_chainTailTitle : m_track;
    const QString seedArtist = fromTail ? m_chainTailArtist : m_artist;
    m_similarAsked = true;
    setSimilarStatus(tr("Finding more like “%1”…").arg(seedTitle));
    qInfo("similar: asking Grok for songs like \"%s\" (need %d, try %d)", qPrintable(seedTitle), need, m_similarTries + 1);
    m_similar->similar(seedTitle, seedArtist, m_recent, qMin(12, need + 2), tasteNote(seedTitle));
}

QString Backend::tasteNote(const QString &request) const {
    return m_useTaste && m_taste ? m_taste->promptNote(request) : QString();
}

// Songs by artists the listener has said they avoid are dropped, unless that would leave too few.
QVariantList Backend::withoutAvoided(const QVariantList &songs) const {
    if (!m_useTaste || !m_taste)
        return songs;
    QVariantList kept;
    for (const auto &value : songs)
        if (!m_taste->avoids(value.toMap().value(QStringLiteral("artist")).toString()))
            kept.append(value);
    return kept.size() >= 10 || kept.size() == songs.size() ? kept : songs;
}

void Backend::setUseTaste(bool on) {
    if (on == m_useTaste)
        return;
    m_useTaste = on;
    if (!m_preview) {
        QSettings prefs(m_dataPath + QStringLiteral("/prefs.conf"), QSettings::IniFormat);
        prefs.setValue(QStringLiteral("taste/use"), on);
    }
    emit tasteChanged();
    showToast(on ? tr("Learning your taste") : tr("Not learning your taste"));
}

// The mood of the mix a song came from, or nothing for a song they picked themselves.
QString Backend::mixFor(const QString &title, const QString &artist) const {
    if (m_mixRequest.isEmpty())
        return {};
    const QString wanted = songKey(title, artist);
    for (const auto &value : m_mixSongs) {
        const QVariantMap song = value.toMap();
        if (songKey(song.value(QStringLiteral("title")).toString(), song.value(QStringLiteral("artist")).toString()) == wanted)
            return m_mixRequest;
    }
    return {};
}

void Backend::rateSong(int rating) {
    if (m_track.isEmpty() || !m_useTaste)
        return;
    rating = qBound(0, rating, 10);
    // Tapping the heart again, on a song already given exactly that rating, takes it back.
    if (m_taste->songRating(m_track, m_artist) == rating)
        rating = 0;
    m_taste->rateSong(m_track, m_artist, rating, mixFor(m_track, m_artist));
    showToast(rating > 0 ? tr("%1/10 for “%2”").arg(rating).arg(m_track) : tr("Rating removed"));
    emit tasteChanged();
}

static QString compactJson(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void Backend::setWantLyrics(bool on) {
    if (on == m_wantLyrics)
        return;
    m_wantLyrics = on;
    emit wantLyricsChanged();
    if (on)
        fetchLyrics();
}

// Words are asked for once per song, and only while something is showing them.
void Backend::fetchLyrics() {
    if (m_trackId.isEmpty() || m_trackId == m_lyricsFor || m_preview)
        return;
    m_lyricsFor = m_trackId;
    m_lyrics->setLoading();
    sendCommand(QStringLiteral("lyrics"),
                compactJson({{QStringLiteral("id"), m_trackId}, {QStringLiteral("title"), m_track}, {QStringLiteral("artist"), m_artist}}), {}, 0);
}

void Backend::setCurrentPlaylist(const QString &id, const QString &name) {
    if (id == m_currentPlaylistId && name == m_currentPlaylistName)
        return;
    m_currentPlaylistId = id;
    m_currentPlaylistName = name;
    emit playlistsChanged();
}

void Backend::refreshPlaylists() {
    if (m_preview || !m_signedIn)
        return;
    m_playlistsLoading = true;
    emit playlistsChanged();
    sendCommand(QStringLiteral("libraryplaylists"), {}, {}, 0);
}

void Backend::addToPlaylist(const QString &playlistId, const QString &name) {
    if (m_trackId.isEmpty() || playlistId.isEmpty())
        return;
    if (m_preview) {
        showToast(tr("Added to %1").arg(name));
        return;
    }
    sendCommand(QStringLiteral("addtoplaylist"),
                compactJson({{QStringLiteral("playlist"), playlistId}, {QStringLiteral("song"), m_trackId}, {QStringLiteral("name"), name}}), {}, 0);
}

void Backend::addToCurrentPlaylist() {
    addToPlaylist(m_currentPlaylistId, m_currentPlaylistName);
}

void Backend::createPlaylist(const QString &name) {
    if (m_trackId.isEmpty())
        return;
    // A name the person typed is theirs, as typed; only a made-up one is put in the house style.
    const QString typed = name.simplified();
    const QString title = typed.isEmpty() ? tidyPlaylistName(suggestPlaylistName(m_track + m_artist)) : typed;
    if (m_preview) {
        showToast(tr("Created %1").arg(title));
        return;
    }
    sendCommand(QStringLiteral("newplaylist"),
                compactJson({{QStringLiteral("name"), title},
                             {QStringLiteral("description"), playlistSummary(title) + QStringLiteral(" Made in Click.")},
                             {QStringLiteral("ids"), QJsonArray{m_trackId}}}), {}, 0);
}

void Backend::toggleLike() {
    if (m_trackId.isEmpty())
        return;
    // The heart in the menu shows "liked" for either kind of like, so one tap flips both to the same side.
    const bool now = !(liked() || songRating() > 0);
    m_likedId = m_trackId;
    m_liked = now;   // at once; Apple's answer only corrects it
    emit likedChanged();
    // rateSong() toggles on a repeated rating, so it is only called when the taste rating must change.
    if (m_useTaste && (songRating() > 0) != now)
        rateSong(now ? 8 : 0);
    if (!m_preview)
        sendCommand(now ? QStringLiteral("love") : QStringLiteral("unlove"), m_trackId, {}, 0);
}

QString Backend::suggestPlaylistName(const QString &seed) const {
    return suggestPlaylistName(QTime::currentTime().hour(), seed);
}

// "3am rain & neon": a time or a mood, then a scene. Picked from a hash, so one song always suggests one name.
QString Backend::suggestPlaylistName(int hour, const QString &seed) {
    static const QStringList moods{QStringLiteral("rain"), QStringLiteral("neon"), QStringLiteral("velvet"), QStringLiteral("slow burn"),
                                   QStringLiteral("golden hour"), QStringLiteral("static"), QStringLiteral("daydream"), QStringLiteral("sub-bass"),
                                   QStringLiteral("midnight"), QStringLiteral("soft focus"), QStringLiteral("overdrive"), QStringLiteral("glass")};
    static const QStringList scenes{QStringLiteral("euphoria"), QStringLiteral("drive"), QStringLiteral("city lights"), QStringLiteral("afterglow"),
                                    QStringLiteral("late train"), QStringLiteral("highway"), QStringLiteral("loft"), QStringLiteral("daze"),
                                    QStringLiteral("skyline"), QStringLiteral("hush"), QStringLiteral("motion"), QStringLiteral("bloom")};
    const uint h = qHash(seed.isEmpty() ? QStringLiteral("click") : seed);
    const QString a = moods.at(int(h % moods.size()));
    const QString b = scenes.at(int((h / 13) % scenes.size()));
    const int pattern = int((h / 7) % 3);
    const int clock = hour % 12 == 0 ? 12 : hour % 12;
    const QString when = QStringLiteral("%1%2").arg(clock).arg(hour < 12 ? QStringLiteral("am") : QStringLiteral("pm"));
    if (pattern == 0 && (hour >= 22 || hour <= 5))
        return QStringLiteral("%1 %2 & %3").arg(when, a, b.section(QLatin1Char(' '), 0, 0));
    if (pattern == 1)
        return QStringLiteral("%1 & %2").arg(a, b);
    return QStringLiteral("%1 %2").arg(a, b);
}

QString Backend::tidyPlaylistName(const QString &name) {
    QStringList words = name.toLower().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    while (words.size() > 4)
        words.removeLast();
    return words.isEmpty() ? QStringLiteral("untitled mix") : words.join(QLatin1Char(' '));
}

QString Backend::playlistSummary(const QString &name) {
    return QStringLiteral("A playlist for “%1”: songs that sit together in mood and tempo.").arg(name.toLower());
}

void Backend::forgetTaste() {
    m_taste->forget();
    showToast(tr("Forgot your taste"));
    emit tasteChanged();
}

void Backend::remember(const QString &title, const QString &artist) {
    if (title.isEmpty())
        return;
    const QString entry = title + QLatin1Char('\n') + artist;
    m_recent.removeAll(entry);
    m_recent.append(entry);
    while (m_recent.size() > 40)
        m_recent.removeFirst();
}

bool Backend::mixAvailable() const {
    return m_mixer && m_mixer->available();
}

void Backend::openMix() {
    if (!m_signedIn || m_signingIn)
        return;
    if (!m_stack.isEmpty() && m_stack.last().kind == QStringLiteral("mix"))
        return;
    emit mixChanged(); // the mood chips follow the clock
    Screen mix;
    mix.id = QStringLiteral("mix");
    mix.kind = QStringLiteral("mix");
    mix.title = tr("Make a Mix");
    push(mix);
}

// Grok picks the songs; the page finds them on Apple Music and plays them.
void Backend::makeMix(const QString &request) {
    if (m_mixBusy || m_preview || request.simplified().isEmpty())
        return;
    m_mixBusy = true;
    setCurrentPlaylist({}, {});
    m_mixSaved = false;
    m_mixIds.clear();
    m_mixSongs.clear();
    m_mixPreview.clear();
    m_mixFirstSent = m_mixFirstPending = m_mixFinished = false;
    m_mixRest.clear();
    m_mixStatus = tr("Asking Grok…");
    m_mixRequest = request.simplified();
    emit mixChanged();
    m_mixer->generate(request, 20, tasteNote(request));
}

// One small block of settings: the song, how far into it, the mix it came from, and how the player was set.
void Backend::saveSession() {
    if (m_preview || !m_restored)
        return;
    // Just after a restore the page reports the start of the song; the saved spot must not be overwritten by it.
    if (m_restoreClock.isValid() && m_restoreClock.elapsed() < 8000)
        return;
    // Nothing playing and nothing remembered yet: keep what an earlier session saved.
    if (m_track.isEmpty() || m_trackId.isEmpty())
        return;
    QSettings prefs(m_dataPath + QStringLiteral("/prefs.conf"), QSettings::IniFormat);
    prefs.setValue(QStringLiteral("session/id"), m_trackId);
    prefs.setValue(QStringLiteral("session/title"), m_track);
    prefs.setValue(QStringLiteral("session/artist"), m_artist);
    prefs.setValue(QStringLiteral("session/position"), m_position);
    prefs.setValue(QStringLiteral("session/mixIds"), m_mixIds);
    prefs.setValue(QStringLiteral("session/volume"), m_volume);
    prefs.setValue(QStringLiteral("session/shuffle"), m_shuffle);
    prefs.setValue(QStringLiteral("session/repeat"), m_repeat);
}

void Backend::restoreSession() {
    if (m_preview || m_restored)
        return;
    QSettings prefs(m_dataPath + QStringLiteral("/prefs.conf"), QSettings::IniFormat);
    const QString id = prefs.value(QStringLiteral("session/id")).toString();
    const bool alreadyPlaying = !m_track.isEmpty() || m_playing;
    m_restored = true;
    if (alreadyPlaying || id.isEmpty())
        return;
    const QStringList ids = prefs.value(QStringLiteral("session/mixIds")).toStringList();
    const double position = prefs.value(QStringLiteral("session/position")).toDouble();
    const QJsonObject snap{{QStringLiteral("id"), id}, {QStringLiteral("position"), position},
                           {QStringLiteral("ids"), QJsonArray::fromStringList(ids)}};
    m_mixIds = ids;
    m_restoreClock.start();
    sendCommand(QStringLiteral("restore"), QString::fromUtf8(QJsonDocument(snap).toJson(QJsonDocument::Compact)), {}, 0);
    // The player's own settings come back too.
    if (prefs.contains(QStringLiteral("session/volume")))
        setVolume(prefs.value(QStringLiteral("session/volume")).toDouble());
    setShuffle(prefs.value(QStringLiteral("session/shuffle")).toBool());
    setRepeatMode(prefs.value(QStringLiteral("session/repeat")).toInt());
}

void Backend::sendMixRest() {
    if (m_mixRest.isEmpty()) {
        m_mixBusy = false;
        m_mixStatus = tr("Playing “%1”").arg(m_mixName);
        emit mixChanged();
        return;
    }
    sendCommand(QStringLiteral("mix"), QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_mixRest)).toJson(QJsonDocument::Compact)),
                QStringLiteral("more"), 0);
    m_mixRest.clear();
}

void Backend::saveMix() {
    if (!mixCanSave())
        return;
    const QString title = tidyPlaylistName(m_mixName);
    const QJsonObject payload{{QStringLiteral("name"), title},
                              {QStringLiteral("description"), playlistSummary(m_mixRequest.isEmpty() ? title : m_mixRequest.toLower()) + QStringLiteral(" Made in Click with Grok.")},
                              {QStringLiteral("ids"), QJsonArray::fromStringList(m_mixIds)}};
    sendCommand(QStringLiteral("savemix"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)), {}, 0);
    m_mixStatus = tr("Saving to your library…");
    emit mixChanged();
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
    if (row.action == QStringLiteral("mix")) {
        openMix();
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
    if (row.action == QStringLiteral("artist")) {
        Screen list;
        list.id = QStringLiteral("artist:") + row.id;
        list.kind = QStringLiteral("list");
        list.title = row.label;
        push(list);
        setStatus(tr("Loading…"));
        queue(QStringLiteral("artist"), row.id);
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
        if (!row.album.isEmpty()) {
            setCurrentPlaylist({}, {});
            // A track inside an album plays the album from there, so what follows is the rest of it.
            queue(QStringLiteral("playalbum"),
                  QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("album"), row.album},
                                                              {QStringLiteral("position"), row.position},
                                                              {QStringLiteral("song"), row.id}})
                                        .toJson(QJsonDocument::Compact)));
        } else {
            // Remember where this started: "add to current playlist" only exists inside one of theirs.
            const bool mine = row.mine && row.type.contains(QStringLiteral("playlist"));
            setCurrentPlaylist(mine ? row.id : QString(), mine ? row.label : QString());
            queue(QStringLiteral("play"), row.id, row.type);
        }
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

// Mute remembers the level it silenced, so the same command brings it back.
void Backend::toggleMute() {
    if (m_volume < 0.001) {
        applyVolume(m_volumeBeforeMute > 0.001 ? m_volumeBeforeMute : 0.5, 80);
        return;
    }
    const double before = m_volume;
    applyVolume(0, 80);
    m_volumeBeforeMute = before;
    emit playbackChanged();
}

void Backend::setRepeatMode(int mode) {
    mode = qBound(0, mode, 2);
    if (mode == m_repeat)
        return;
    m_repeat = mode;
    emit playbackChanged();
    if (!m_preview)
        sendCommand(QStringLiteral("repeat"), QString::number(mode), {}, 0);
}

// Off, then the whole queue, then just this song.
void Backend::cycleRepeat() {
    setRepeatMode(m_repeat == 0 ? 2 : m_repeat == 2 ? 1 : 0);
}

void Backend::setShuffle(bool on) {
    if (on == m_shuffle)
        return;
    m_shuffle = on;
    emit playbackChanged();
    if (!m_preview)
        sendCommand(QStringLiteral("shuffle"), on ? QStringLiteral("1") : QStringLiteral("0"), {}, 0);
}

void Backend::seekBy(double seconds) {
    seek(m_position + seconds);
}

void Backend::setVolume(double value) { applyVolume(value, 0); }

// `rampMs` > 0 asks the page to glide there (mute and unmute use 80 ms so nothing pops).
void Backend::applyVolume(double value, int rampMs) {
    const double next = qBound(0.0, value, 1.0);
    if (qAbs(next - m_volume) < 0.001)
        return;
    m_volumeBeforeMute = 0;
    m_volume = next;
    m_volumeSetAt.restart();
    emit playbackChanged();
    emit volumeNudged();
    queue(QStringLiteral("volume"),
          rampMs > 0 ? QStringLiteral("%1,%2").arg(m_volume, 0, 'f', 3).arg(rampMs) : QString::number(m_volume, 'f', 3));
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

void Backend::rememberSearch(const QString &text) {
    const QString clean = text.simplified();
    if (clean.size() < 2 || m_preview)
        return;
    m_history.removeAll(clean);
    // "ivy" typed after "iv": the shorter one was only on the way there.
    m_history.removeIf([&](const QString &old) { return clean.startsWith(old, Qt::CaseInsensitive) && old.size() < clean.size(); });
    m_history.prepend(clean);
    while (m_history.size() > 8)
        m_history.removeLast();
    QSettings prefs(m_dataPath + QStringLiteral("/prefs.conf"), QSettings::IniFormat);
    prefs.setValue(QStringLiteral("search/history"), m_history);
    emit historyChanged();
}

void Backend::forgetSearch(const QString &text) {
    if (text.isEmpty())
        m_history.clear();
    else
        m_history.removeAll(text);
    if (!m_preview) {
        QSettings prefs(m_dataPath + QStringLiteral("/prefs.conf"), QSettings::IniFormat);
        prefs.setValue(QStringLiteral("search/history"), m_history);
    }
    emit historyChanged();
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
    m_historyPending = text;
    m_historyTimer.start();
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
            QString album;
            int position = -1;
            if (rowType == QStringLiteral("queueitem")) {
                action = QStringLiteral("jump");
            } else if (row.value(QStringLiteral("open")).toBool()) {
                // The page says this row opens another screen, and which (an artist's albums, an album's tracks).
                action = QStringLiteral("open");
                path = row.value(QStringLiteral("path")).toString(id);
            } else if (rowType == QStringLiteral("artists")) {
                action = QStringLiteral("artist"); // a catalog artist: their own page
            } else if (rowType.contains(QStringLiteral("artist"))) {
                action = QStringLiteral("open");
                path = QStringLiteral("/v1/me/library/artists/") + id + QStringLiteral("/albums");
            } else if (row.contains(QStringLiteral("album"))) {
                album = row.value(QStringLiteral("album")).toString();
                position = row.value(QStringLiteral("position")).toInt(-1);
            }
            rows.append(Row{label, row.value(QStringLiteral("detail")).toString(), action, rowType, path, album, position, row.value(QStringLiteral("mine")).toBool()});
        }
        // An album's tracks come with a "Play album" row on top.
        const QJsonObject header = object.value(QStringLiteral("header")).toObject();
        if (!header.isEmpty() && !header.value(QStringLiteral("id")).toString().isEmpty())
            rows.prepend(Row{header.value(QStringLiteral("label")).toString(), header.value(QStringLiteral("detail")).toString(),
                             QStringLiteral("play"), header.value(QStringLiteral("type")).toString(),
                             header.value(QStringLiteral("id")).toString()});
        m_stack.last().rows = rows;
        m_stack.last().selected = 0;
        setStatus(rows.isEmpty() ? tr("Nothing here") : QString());
        emit screenChanged();
        return;
    }

    if (type == QStringLiteral("similar")) {
        const QString stateName = object.value(QStringLiteral("state")).toString();
        if (stateName == QStringLiteral("added")) {
            QJsonArray items = object.value(QStringLiteral("items")).toArray();
            if (items.isEmpty())
                items.append(QJsonObject{{QStringLiteral("title"), object.value(QStringLiteral("title")).toString()},
                                         {QStringLiteral("artist"), object.value(QStringLiteral("artist")).toString()},
                                         {QStringLiteral("id"), object.value(QStringLiteral("id")).toString()}});
            QString firstTitle, firstArtist;
            for (const auto &value : items) {
                const QJsonObject item = value.toObject();
                const QString title = item.value(QStringLiteral("title")).toString();
                const QString artist = item.value(QStringLiteral("artist")).toString();
                if (firstTitle.isEmpty()) {
                    firstTitle = title;
                    firstArtist = artist;
                }
                remember(title, artist);
                m_autoAdded.append(songKey(title, artist));
                m_autoAddedTitles.append(looseTitle(title));
                if (!item.value(QStringLiteral("id")).toString().isEmpty())
                    m_autoAddedIds.append(item.value(QStringLiteral("id")).toString());
                m_chainTailTitle = title;
                m_chainTailArtist = artist;
            }
            while (m_autoAddedTitles.size() > 60)
                m_autoAddedTitles.removeFirst();
            while (m_autoAdded.size() > 60)
                m_autoAdded.removeFirst();
            while (m_autoAddedIds.size() > 60)
                m_autoAddedIds.removeFirst();
            m_lastCandidates.clear();
            m_similarAsked = false;
            m_similarTries = 0;
            qInfo("similar: queued %d song(s), first \"%s\" by %s", int(items.size()), qPrintable(firstTitle), qPrintable(firstArtist));
            setSimilarStatus(items.size() > 1 ? tr("Queued %1 songs, starting with %2").arg(items.size()).arg(firstTitle)
                                              : tr("Up next: %1 · %2").arg(firstTitle, firstArtist));
            showToast(items.size() > 1 ? tr("Queued %1 similar songs").arg(items.size()) : tr("Up next: %1").arg(firstTitle));
            // Look again once the page has reported its new queue: still short of ten, ask for more at once.
            m_similarTimer.start(2500);
        } else if (stateName == QStringLiteral("error")) {
            const QString message = object.value(QStringLiteral("message")).toString();
            qInfo("similar: the page could not queue it: %s", qPrintable(message));
            retrySimilar(message.isEmpty() ? tr("Could not find a similar song.") : message);
        }
        return;
    }

    if (type == QStringLiteral("lyrics")) {
        // Late answers for a song that has already gone by are dropped.
        if (object.value(QStringLiteral("id")).toString() != m_lyricsFor)
            return;
        const QString words = object.value(QStringLiteral("text")).toString();
        if (words.trimmed().isEmpty())
            m_lyrics->setNone();
        else
            m_lyrics->setText(words);
        return;
    }
    if (type == QStringLiteral("playlists")) {
        QVariantList list;
        for (const auto &value : object.value(QStringLiteral("items")).toArray()) {
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("id")).toString().isEmpty())
                continue;
            list.append(QVariantMap{{QStringLiteral("id"), item.value(QStringLiteral("id")).toString()},
                                    {QStringLiteral("name"), item.value(QStringLiteral("name")).toString()},
                                    {QStringLiteral("clickMade"), item.value(QStringLiteral("clickMade")).toBool()}});
        }
        m_playlists = list;
        m_playlistsLoading = false;
        emit playlistsChanged();
        return;
    }
    if (type == QStringLiteral("rating")) {
        if (object.value(QStringLiteral("id")).toString() == m_trackId) {
            m_likedId = m_trackId;
            m_liked = object.value(QStringLiteral("loved")).toBool();
            emit likedChanged();
        }
        return;
    }
    if (type == QStringLiteral("playlist")) {
        const QString stateName = object.value(QStringLiteral("state")).toString();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (stateName == QStringLiteral("added")) {
            showToast(tr("Added to %1").arg(name));
        } else if (stateName == QStringLiteral("created")) {
            showToast(tr("Created %1").arg(name));
            refreshPlaylists();
        } else {
            m_playlistsLoading = false;
            emit playlistsChanged();
            const QString message = object.value(QStringLiteral("message")).toString();
            showToast(message.isEmpty() ? tr("Something went wrong") : message);
        }
        return;
    }
    if (type == QStringLiteral("mix")) {
        const QString stateName = object.value(QStringLiteral("state")).toString();
        if (stateName == QStringLiteral("progress")) {
            m_mixStatus = tr("Finding songs… %1 of %2")
                              .arg(object.value(QStringLiteral("found")).toInt())
                              .arg(object.value(QStringLiteral("total")).toInt());
        } else if (stateName == QStringLiteral("started")) {
            // The first song is playing.
            m_mixFirstPending = false;
            m_mixIds.clear();
            for (const auto &id : object.value(QStringLiteral("ids")).toArray())
                m_mixIds.append(id.toString());
            m_mixStatus = tr("Playing — Grok is still picking more…");
            emit mixChanged();
            showNowPlaying();
            if (m_mixFinished)
                sendMixRest();
            return;
        } else if (stateName == QStringLiteral("firstfailed")) {
            // Not on Apple Music: fall back to the whole mix when Grok is done.
            m_mixFirstSent = m_mixFirstPending = false;
            if (m_mixFinished) {
                m_mixStatus = tr("Finding %1 songs on Apple Music…").arg(m_mixSongs.size());
                sendCommand(QStringLiteral("mix"), QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(m_mixSongs)).toJson(QJsonDocument::Compact)),
                            m_mixName, 0);
            }
        } else if (stateName == QStringLiteral("done")) {
            if (object.value(QStringLiteral("more")).toBool()) {
                for (const auto &id : object.value(QStringLiteral("ids")).toArray())
                    if (!m_mixIds.contains(id.toString()))
                        m_mixIds.append(id.toString());
            } else {
                m_mixIds.clear();
                for (const auto &id : object.value(QStringLiteral("ids")).toArray())
                    m_mixIds.append(id.toString());
            }
            m_mixBusy = false;
            m_mixStatus = tr("Playing “%1”, %2 songs").arg(m_mixName).arg(m_mixIds.size());
            emit mixChanged();
            showToast(tr("Mix ready"));
            if (!m_mixFirstSent)
                showNowPlaying();
            return;
        } else if (stateName == QStringLiteral("saved")) {
            m_mixSaved = true;
            m_mixStatus = tr("Saved “%1” to your library").arg(m_mixName);
            showToast(tr("Saved to library"));
        } else if (stateName == QStringLiteral("error")) {
            m_mixBusy = false;
            const QString message = object.value(QStringLiteral("message")).toString();
            m_mixStatus = message.isEmpty() ? tr("Could not build the mix.") : message;
        }
        emit mixChanged();
        return;
    }

    if (type != QStringLiteral("state"))
        return;

    const bool wasSignedIn = m_signedIn;
    const bool wasPlaying = m_playing;
    const QString oldTitle = m_track;
    const QString oldArtist = m_artist;
    const double oldFraction = m_furthest > 0 ? m_furthest : -1;
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
    const QString previousArtwork = m_artwork;
    takeString("artwork", m_artwork);
    if (m_artwork != previousArtwork)
        fetchArtColor();
    const QString previousTrackId = m_trackId;
    takeString("trackId", m_trackId);
    if (m_trackId != previousTrackId && m_wantLyrics)
        fetchLyrics();
    if (m_trackId != previousTrackId && !m_trackId.isEmpty() && !m_preview) {
        m_liked = false;
        emit likedChanged();
        sendCommand(QStringLiteral("rating"), m_trackId, {}, 0);
    }
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
    // The page echoes the level it had a moment ago; right after the user moved it, that is stale.
    if (!m_volumeSetAt.isValid() || m_volumeSetAt.elapsed() > 900)
        takeDouble("volume", m_volume);
    if (object.value(QStringLiteral("repeat")).isDouble()) {
        const int mode = qBound(0, object.value(QStringLiteral("repeat")).toInt(), 2);
        if (mode != m_repeat) {
            m_repeat = mode;
            playback = true;
        }
    }
    takeBool("shuffle", m_shuffle);
    // How far this song has got. Only while it is still the same song: when the page starts reporting the
    // next song's position a poll before its title, that must not make a finished song look skipped.
    if (m_track == previousTitle && m_duration > 5)
        m_furthest = qMax(m_furthest, m_position / m_duration);
    if (object.value(QStringLiteral("upcoming")).isDouble())
        m_upcoming = object.value(QStringLiteral("upcoming")).toInt();
    if (object.value(QStringLiteral("queueLength")).isDouble())
        m_queueLength = object.value(QStringLiteral("queueLength")).toInt();
    if (object.value(QStringLiteral("queuePosition")).isDouble())
        m_queuePosition = object.value(QStringLiteral("queuePosition")).toInt();
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
        if (m_signedIn && m_autoSimilar)
            syncAutoplay();
        if (m_signedIn)
            restoreSession();
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
    if (titleArrived) {
        // What the last song's ending said: skipped at once, or played to the end.
        if (m_useTaste && !oldTitle.isEmpty())
            m_taste->listened(oldTitle, oldArtist, oldFraction, mixFor(oldTitle, oldArtist));
        m_heard = 0;
        m_heardCounted = false;
        saveSession();
        emit tasteChanged(); // the thumbs follow the song
        m_furthest = m_duration > 5 ? m_position / m_duration : 0; // the new song starts from where it is
        trackChangedForSimilar();
    }
    if (m_signedIn && m_stack.size() == 1 && (titleArrived || started))
        showNowPlaying();
    if (playback)
        emit playbackChanged();
}

QVariantList Backend::discoveriesAt(int hour) const {
    auto chip = [](const QString &label, const QString &request) {
        return QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("request"), request}};
    };
    QVariantList out;
    if (hour >= 5 && hour < 11)
        out << chip(tr("Morning coffee"), QStringLiteral("gentle, warm songs for a slow morning coffee"));
    else if (hour >= 11 && hour < 17)
        out << chip(tr("Afternoon focus"), QStringLiteral("steady, mostly instrumental songs to focus in the afternoon"));
    else if (hour >= 17 && hour < 22)
        out << chip(tr("Evening glow"), QStringLiteral("feel-good evening songs, upbeat but not loud"));
    else
        out << chip(tr("Late night"), QStringLiteral("calm, atmospheric late-night songs"));
    if (m_useTaste) {
        for (const QString &artist : m_taste->topArtists(2))
            out << chip(tr("More like %1").arg(artist), QStringLiteral("songs that sound like %1, mostly by other artists").arg(artist));
    }
    out << chip(tr("Surprise me"), QStringLiteral("a surprising mix of songs I probably have not heard, that suit my taste"));
    return out;
}
