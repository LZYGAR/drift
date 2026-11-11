#include "mpris.h"

#include "backend.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDebug>
#include <QGuiApplication>
#include <QWindow>
#include <QtNumeric>

namespace {

const QString kObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString kService = QStringLiteral("org.mpris.MediaPlayer2.drift");
const QString kPlayer = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString kProperties = QStringLiteral("org.freedesktop.DBus.Properties");

} // namespace

QString MprisState::playbackStatus(bool playing, const QString &track) {
    if (playing)
        return QStringLiteral("Playing");
    if (!track.isEmpty())
        return QStringLiteral("Paused");
    return QStringLiteral("Stopped");
}

QString MprisState::trackPath(const QString &track, const QString &artist, const QString &album) {
    if (track.isEmpty())
        return QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");
    // Object-path bytes cannot carry a title, so the id is a hash of the song.
    const QByteArray bytes = (track + QLatin1Char('\n') + artist + QLatin1Char('\n') + album).toUtf8();
    quint32 hash = 2166136261u;
    for (int i = 0; i < bytes.size(); ++i) {
        hash ^= quint32(quint8(bytes.at(i)));
        hash *= 16777619u;
    }
    return QStringLiteral("/org/mpris/MediaPlayer2/Track/%1").arg(hash, 0, 16);
}

qint64 MprisState::secondsToMicros(double seconds) {
    if (!(seconds > 0.0))
        return 0;
    return qint64(seconds * 1000000.0 + 0.5);
}

QVariantMap MprisState::metadata(const QString &track, const QString &artist, const QString &album,
                                 const QString &artwork, double durationSeconds) {
    QVariantMap map;
    map.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(trackPath(track, artist, album))));
    if (!track.isEmpty())
        map.insert(QStringLiteral("xesam:title"), track);
    if (!artist.isEmpty())
        map.insert(QStringLiteral("xesam:artist"), QStringList{artist});
    if (!album.isEmpty())
        map.insert(QStringLiteral("xesam:album"), album);
    if (!artwork.isEmpty())
        map.insert(QStringLiteral("mpris:artUrl"), artwork);
    if (durationSeconds > 0.0)
        map.insert(QStringLiteral("mpris:length"), QVariant::fromValue(qlonglong(secondsToMicros(durationSeconds))));
    return map;
}

QString MprisState::loopStatus(int repeat) {
    return repeat == 1 ? QStringLiteral("Track") : repeat == 2 ? QStringLiteral("Playlist") : QStringLiteral("None");
}

int MprisState::repeatFromLoop(const QString &loop) {
    return loop == QStringLiteral("Track") ? 1 : loop == QStringLiteral("Playlist") ? 2 : 0;
}

MprisUpdate MprisState::diff(const MprisView &before, const MprisView &after) {
    MprisUpdate update;
    const QString was = playbackStatus(before.playing, before.track);
    const QString now = playbackStatus(after.playing, after.track);
    if (was != now)
        update.properties.insert(QStringLiteral("PlaybackStatus"), now);
    const bool sameSong = before.track == after.track && before.artist == after.artist && before.album == after.album;
    const bool metaChanged = !sameSong || before.artwork != after.artwork
                             || !qFuzzyCompare(before.duration + 1.0, after.duration + 1.0);
    if (metaChanged) {
        update.properties.insert(QStringLiteral("Metadata"),
                                 metadata(after.track, after.artist, after.album, after.artwork, after.duration));
    }
    if (qAbs(before.volume - after.volume) >= 0.001)
        update.properties.insert(QStringLiteral("Volume"), after.volume);
    if (before.repeat != after.repeat)
        update.properties.insert(QStringLiteral("LoopStatus"), loopStatus(after.repeat));
    if (before.shuffle != after.shuffle)
        update.properties.insert(QStringLiteral("Shuffle"), after.shuffle);

    const double delta = after.position - before.position;
    const bool moved = !qFuzzyCompare(before.position + 1.0, after.position + 1.0);
    // The playback clock steps +0.1s and emits playbackChanged. That is not a seek.
    const bool tick = after.playing && sameSong && delta > 0.05 && delta < 0.2;
    update.seeked = moved && sameSong && !tick;
    return update;
}

class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Drift"); }
    QString desktopEntry() const { return QStringLiteral("drift"); }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }

public slots:
    void Raise() {
        const auto windows = QGuiApplication::topLevelWindows();
        for (QWindow *window : windows) {
            if (!window->isVisible())
                continue;
            const auto states = window->windowStates();
            if (states.testFlag(Qt::WindowMinimized))
                window->setWindowStates(states & ~Qt::WindowMinimized);
            window->raise();
            window->requestActivate();
        }
    }

    void Quit() { QCoreApplication::quit(); }
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(double Rate READ rate)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canControl)
    Q_PROPERTY(bool CanGoPrevious READ canControl)
    Q_PROPERTY(bool CanPlay READ canControl)
    Q_PROPERTY(bool CanPause READ canControl)
    Q_PROPERTY(bool CanSeek READ canControl)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MprisPlayerAdaptor(Mpris *parent) : QDBusAbstractAdaptor(parent) {}

    void sendSeeked(qlonglong position) { emit Seeked(position); }

    QString playbackStatus() const { return owner()->playbackStatus(); }
    QVariantMap metadata() const { return owner()->metadata(); }
    double volume() const { return owner()->volume(); }
    void setVolume(double value) { owner()->setVolume(value); }
    qlonglong position() const { return owner()->position(); }
    QString loopStatus() const { return owner()->loopStatus(); }
    void setLoopStatus(const QString &loop) { owner()->setLoopStatus(loop); }
    bool shuffle() const { return owner()->shuffle(); }
    void setShuffle(bool on) { owner()->setShuffle(on); }
    double rate() const { return 1.0; }
    bool canControl() const { return true; }

public slots:
    void Next() { owner()->next(); }
    void Previous() { owner()->previous(); }
    void Pause() { owner()->pause(); }
    // Apple's player has no stop; pausing is the closest honest mapping.
    void Stop() { owner()->pause(); }
    void PlayPause() { owner()->playPause(); }
    void Play() { owner()->play(); }
    void Seek(qlonglong Offset) { owner()->seekBy(Offset); }
    void SetPosition(const QDBusObjectPath &TrackId, qlonglong Position) {
        owner()->setPositionMicros(TrackId.path(), Position);
    }

signals:
    void Seeked(qlonglong Position);

private:
    Mpris *owner() const { return static_cast<Mpris *>(parent()); }
};

Mpris::Mpris(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend) {
    new MprisRootAdaptor(this);
    m_player = new MprisPlayerAdaptor(this);
    m_last = capture();
    connect(m_backend, &Backend::playbackChanged, this, [this] { publish(); });
}

bool Mpris::registerService() {
    if (m_registered)
        return true;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning("MPRIS: session bus is unavailable");
        return false;
    }
    if (!bus.registerObject(kObjectPath, this, QDBusConnection::ExportAdaptors)) {
        const QString message = bus.lastError().isValid() ? bus.lastError().message()
                                                          : QStringLiteral("could not register the player object");
        qWarning("MPRIS: %s", qPrintable(message));
        return false;
    }
    if (!bus.registerService(kService)) {
        const QString message = bus.lastError().isValid() ? bus.lastError().message()
                                                          : QStringLiteral("could not own org.mpris.MediaPlayer2.drift");
        qWarning("MPRIS: %s", qPrintable(message));
        bus.unregisterObject(kObjectPath);
        return false;
    }
    m_registered = true;
    return true;
}

void Mpris::publish() {
    const MprisView now = capture();
    const MprisUpdate update = MprisState::diff(m_last, now);
    m_last = now;
    if (!m_registered)
        return;
    if (!update.properties.isEmpty()) {
        QDBusMessage signal = QDBusMessage::createSignal(kObjectPath, kProperties, QStringLiteral("PropertiesChanged"));
        signal << kPlayer << update.properties << QStringList();
        QDBusConnection::sessionBus().send(signal);
    }
    if (update.seeked && m_player)
        m_player->sendSeeked(MprisState::secondsToMicros(now.position));
}

MprisView Mpris::capture() const {
    MprisView view;
    view.playing = m_backend->playing();
    view.track = m_backend->track();
    view.artist = m_backend->artist();
    view.album = m_backend->album();
    // The saved copy when there is one: widgets read a file more reliably than https.
    view.artwork = m_backend->artworkLocal().isEmpty() ? m_backend->artwork() : m_backend->artworkLocal();
    view.repeat = m_backend->repeatMode();
    view.shuffle = m_backend->shuffle();
    view.position = m_backend->position();
    view.duration = m_backend->duration();
    view.volume = m_backend->volume();
    return view;
}

void Mpris::play() {
    if (!m_backend->playing())
        m_backend->playPause();
}

void Mpris::pause() {
    if (m_backend->playing())
        m_backend->playPause();
}

void Mpris::playPause() {
    m_backend->playPause();
}

void Mpris::next() {
    m_backend->nextTrack();
}

void Mpris::previous() {
    m_backend->previousTrack();
}

void Mpris::seekBy(qlonglong offsetMicros) {
    const double target = m_backend->position() + double(offsetMicros) / 1000000.0;
    m_backend->seek(target);
}

void Mpris::setPositionMicros(const QString &trackId, qlonglong positionMicros) {
    const QString current = MprisState::trackPath(m_backend->track(), m_backend->artist(), m_backend->album());
    if (trackId != current || positionMicros < 0)
        return;
    const double seconds = double(positionMicros) / 1000000.0;
    const double duration = m_backend->duration();
    if (duration > 0.0 && seconds > duration)
        return;
    m_backend->seek(seconds);
}

QString Mpris::playbackStatus() const {
    return MprisState::playbackStatus(m_backend->playing(), m_backend->track());
}

QVariantMap Mpris::metadata() const {
    const MprisView view = capture();
    return MprisState::metadata(view.track, view.artist, view.album, view.artwork, view.duration);
}

double Mpris::volume() const {
    return m_backend->volume();
}

void Mpris::setVolume(double value) {
    if (qIsNaN(value))
        return;
    m_backend->setVolume(value);
}

QString Mpris::loopStatus() const {
    return MprisState::loopStatus(m_backend->repeatMode());
}

void Mpris::setLoopStatus(const QString &loop) {
    m_backend->setRepeatMode(MprisState::repeatFromLoop(loop));
}

bool Mpris::shuffle() const {
    return m_backend->shuffle();
}

void Mpris::setShuffle(bool on) {
    m_backend->setShuffle(on);
}

qlonglong Mpris::position() const {
    return MprisState::secondsToMicros(m_backend->position());
}

#include "mpris.moc"
