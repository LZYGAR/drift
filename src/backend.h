#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVector>

// The only object music.apple.com can call. It accepts playback reports and
// nothing that touches the disk.
class Bridge : public QObject {
    Q_OBJECT
public:
    explicit Bridge(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void ingest(const QString &json);

signals:
    void received(const QString &json);
};

// Click-wheel menus and the Apple Music session behind them. The page plays
// the audio; this object only keeps the screen and sends commands.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *bridge READ bridge CONSTANT)
    Q_PROPERTY(QString dataPath READ dataPath CONSTANT)
    Q_PROPERTY(QString shotPath READ shotPath CONSTANT)
    Q_PROPERTY(bool preview READ preview CONSTANT)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY sessionChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY sessionChanged)
    Q_PROPERTY(QString status READ status NOTIFY sessionChanged)
    Q_PROPERTY(QString title READ title NOTIFY screenChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY screenChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY screenChanged)
    Q_PROPERTY(int selected READ selected NOTIFY screenChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(QString track READ track NOTIFY playbackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY playbackChanged)
    Q_PROPERTY(QString album READ album NOTIFY playbackChanged)
    Q_PROPERTY(QString artwork READ artwork NOTIFY playbackChanged)
    Q_PROPERTY(QString trackId READ trackId NOTIFY playbackChanged)
    Q_PROPERTY(double position READ position NOTIFY playbackChanged)
    Q_PROPERTY(double duration READ duration NOTIFY playbackChanged)
    Q_PROPERTY(QString positionLabel READ positionLabel NOTIFY playbackChanged)
    Q_PROPERTY(QString durationLabel READ durationLabel NOTIFY playbackChanged)
    Q_PROPERTY(double volume READ volume NOTIFY playbackChanged)
    Q_PROPERTY(QString command READ command NOTIFY commandReady)
    Q_PROPERTY(QString toast READ toast NOTIFY toastChanged)
    Q_PROPERTY(bool mixAvailable READ mixAvailable CONSTANT)
    Q_PROPERTY(bool mixBusy READ mixBusy NOTIFY mixChanged)
    Q_PROPERTY(QString mixStatus READ mixStatus NOTIFY mixChanged)
    Q_PROPERTY(bool mixCanSave READ mixCanSave NOTIFY mixChanged)

public:
    explicit Backend(bool preview = false, QObject *parent = nullptr);

    QObject *bridge() const { return m_bridge; }
    QString dataPath() const { return m_dataPath; }
    QString shotPath() const { return m_shotPath; }
    bool preview() const { return m_preview; }
    bool signingIn() const { return m_signingIn; }
    bool signedIn() const { return m_signedIn; }
    QString status() const { return m_status; }
    QString title() const;
    QString kind() const;
    QVariantList rows() const;
    int selected() const;
    bool playing() const { return m_playing; }
    QString track() const { return m_track; }
    QString artist() const { return m_artist; }
    QString album() const { return m_album; }
    QString artwork() const { return m_artwork; }
    QString trackId() const { return m_trackId; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    QString positionLabel() const { return formatTime(m_position); }
    QString durationLabel() const { return formatTime(m_duration); }
    double volume() const { return m_volume; }
    QString command() const { return m_command; }
    QString toast() const { return m_toast; }
    bool mixAvailable() const;
    bool mixBusy() const { return m_mixBusy; }
    QString mixStatus() const { return m_mixStatus; }
    bool mixCanSave() const { return !m_mixIds.isEmpty() && !m_mixSaved; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void wheel(int steps);
    Q_INVOKABLE void center();
    Q_INVOKABLE void menu();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void nextTrack();
    Q_INVOKABLE void previousTrack();
    Q_INVOKABLE void nudgeVolume(int steps);
    Q_INVOKABLE void setVolume(double value);
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void openPlayer();
    Q_INVOKABLE void openSearch();
    Q_INVOKABLE void openQueue();
    Q_INVOKABLE void openMix();
    Q_INVOKABLE void makeMix(const QString &request);
    Q_INVOKABLE void saveMix();
    Q_INVOKABLE void addToQueue(int index, bool playNext);
    Q_INVOKABLE void back();
    Q_INVOKABLE void setSearch(const QString &text);
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void retranslate();

    static QString formatTime(double seconds);
    static QString jsString(const QString &text);

signals:
    void sessionChanged();
    void screenChanged();
    void playbackChanged();
    void volumeNudged();
    void commandReady();
    void toastChanged();
    void mixChanged();

private:
    struct Row {
        QString label;
        QString detail;
        QString action;
        QString type;
        QString id;
    };
    struct Screen {
        QString id;
        QString title;
        QString kind;
        QVector<Row> rows;
        int selected = 0;
    };

    void ingest(const QString &json);
    void setStatus(const QString &status);
    void rebuildRoot();
    void push(Screen screen);
    void queue(const QString &command, const QString &arg = {}, const QString &type = {});
    void sendCommand(const QString &command, const QString &arg, const QString &type, int token);
    void showToast(const QString &text);
    void showNowPlaying();
    Screen musicScreen() const;
    Screen rootScreen() const;
    QVariantList rowsOf(const Screen &screen) const;

    Bridge *m_bridge;
    QString m_dataPath;
    QString m_shotPath;
    bool m_preview = false;
    bool m_signingIn = false;
    bool m_signedIn = false;
    QString m_status;
    QString m_notice;
    QVector<Screen> m_stack;

    bool m_playing = false;
    QString m_track;
    QString m_artist;
    QString m_album;
    QString m_artwork;
    QString m_trackId;
    double m_position = 0;
    double m_duration = 0;
    double m_volume = 1;
    QString m_command;
    QString m_toast;
    class Mixer *m_mixer = nullptr;
    bool m_mixBusy = false;
    bool m_mixSaved = false;
    QString m_mixStatus;
    QString m_mixName;
    QStringList m_mixIds;
    int m_serial = 0;
    int m_activeToken = 0;
};
