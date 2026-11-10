#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class Backend;
class MprisPlayerAdaptor;

// One frame of Backend playback, flattened so it can be compared without the bus.
struct MprisView {
    bool playing = false;
    QString track;
    QString artist;
    QString album;
    QString artwork;
    double position = 0;
    double duration = 0;
    double volume = 1;
};

// What a playbackChanged edge should publish. Position is polled, so it is never a key.
struct MprisUpdate {
    QVariantMap properties;
    bool seeked = false;
};

// Pure MPRIS2 mapping. Tests exercise this without a session bus.
class MprisState {
public:
    static QString playbackStatus(bool playing, const QString &track);
    static QString trackPath(const QString &track, const QString &artist, const QString &album);
    static QVariantMap metadata(const QString &track, const QString &artist, const QString &album,
                                const QString &artwork, double durationSeconds);
    static qint64 secondsToMicros(double seconds);
    static MprisUpdate diff(const MprisView &before, const MprisView &after);
};

// org.mpris.MediaPlayer2.click on the session bus. Registration failure is non-fatal.
class Mpris : public QObject {
    Q_OBJECT
    friend class MprisPlayerAdaptor;

public:
    explicit Mpris(Backend *backend, QObject *parent = nullptr);

    // False when the bus is down or the well-known name is already taken. Logs once.
    bool registerService();

private:
    void publish();
    MprisView capture() const;
    void play();
    void pause();
    void playPause();
    void next();
    void previous();
    void seekBy(qlonglong offsetMicros);
    void setPositionMicros(const QString &trackId, qlonglong positionMicros);
    QString playbackStatus() const;
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double value);
    qlonglong position() const;

    Backend *m_backend;
    MprisPlayerAdaptor *m_player = nullptr;
    MprisView m_last;
    bool m_registered = false;
};
