#pragma once

#include <QElapsedTimer>

#include <QColor>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include "lyrics.h"
#include "stats.h"
#include "taste.h"
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
    Q_PROPERTY(bool previewMini READ previewMini CONSTANT)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY sessionChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY sessionChanged)
    // True once Apple Music's page has said whether you are signed in (or after a while, so a page that never loads
    // does not keep the window hidden or the opening waiting for ever).
    Q_PROPERTY(bool sessionKnown READ sessionKnown NOTIFY sessionChanged)
    Q_PROPERTY(bool introEnabled READ introEnabled WRITE setIntroEnabled NOTIFY introChanged)
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
    Q_PROPERTY(QColor artColor READ artColor NOTIFY artColorChanged)
    Q_PROPERTY(bool hasArtColor READ hasArtColor NOTIFY artColorChanged)
    Q_PROPERTY(QColor artSecondary READ artSecondary NOTIFY artColorChanged)
    Q_PROPERTY(QColor artAmbient READ artAmbient NOTIFY artColorChanged)
    Q_PROPERTY(QString artworkLocal READ artworkLocal NOTIFY playbackChanged)
    Q_PROPERTY(int repeatMode READ repeatMode NOTIFY playbackChanged)
    Q_PROPERTY(bool shuffle READ shuffle NOTIFY playbackChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY playbackChanged)
    Q_PROPERTY(QString previewPanel READ previewPanel CONSTANT)
    Q_PROPERTY(bool autoSimilar READ autoSimilar WRITE setAutoSimilar NOTIFY similarChanged)
    Q_PROPERTY(bool similarAvailable READ similarAvailable CONSTANT)
    Q_PROPERTY(QString similarStatus READ similarStatus NOTIFY similarChanged)
    Q_PROPERTY(bool useTaste READ useTaste WRITE setUseTaste NOTIFY tasteChanged)
    Q_PROPERTY(QString tasteSummary READ tasteSummary NOTIFY tasteChanged)
    // One-click mix ideas for the empty search screen: the time of day, and the artists liked most.
    Q_PROPERTY(QVariantList discoveries READ discoveries NOTIFY tasteChanged)
    Q_PROPERTY(int songRating READ songRating NOTIFY tasteChanged)
    // Playlists the person manages themselves (never Apple's), for the + menu.
    Q_PROPERTY(QVariantList playlists READ playlists NOTIFY playlistsChanged)
    Q_PROPERTY(bool playlistsLoading READ playlistsLoading NOTIFY playlistsChanged)
    // The playlist the queue was started from, but only if it is one of theirs; empty otherwise.
    Q_PROPERTY(QString currentPlaylist READ currentPlaylist NOTIFY playlistsChanged)
    // Apple Music's heart (a rating of 1) on the song that is playing.
    Q_PROPERTY(QStringList searchHistory READ searchHistory NOTIFY historyChanged)
    Q_PROPERTY(QObject *stats READ statsObject CONSTANT)
    Q_PROPERTY(bool liked READ liked NOTIFY likedChanged)
    Q_PROPERTY(QObject *lyrics READ lyricsObject CONSTANT)
    // The lyrics view sets this while it is on screen, so words are only fetched when someone can read them.
    Q_PROPERTY(bool wantLyrics READ wantLyrics WRITE setWantLyrics NOTIFY wantLyricsChanged)
    Q_PROPERTY(bool mixAvailable READ mixAvailable CONSTANT)
    Q_PROPERTY(bool mixBusy READ mixBusy NOTIFY mixChanged)
    Q_PROPERTY(QString mixStatus READ mixStatus NOTIFY mixChanged)
    Q_PROPERTY(bool mixCanSave READ mixCanSave NOTIFY mixChanged)
    Q_PROPERTY(QString mixPreview READ mixPreview NOTIFY mixChanged)
    Q_PROPERTY(QVariantList mixSongs READ mixSongs NOTIFY mixChanged)
    Q_PROPERTY(QStringList mixChips READ mixChips NOTIFY mixChanged)

public:
    explicit Backend(bool preview = false, QObject *parent = nullptr);

    QObject *bridge() const { return m_bridge; }
    QString dataPath() const { return m_dataPath; }
    QString shotPath() const { return m_shotPath; }
    bool preview() const { return m_preview; }
    bool previewMini() const { return m_preview && qEnvironmentVariableIsSet("CLICK_PREVIEW_MINI"); }
    bool signingIn() const { return m_signingIn; }
    bool signedIn() const { return m_signedIn; }
    bool sessionKnown() const { return m_sessionKnown; }
    bool introEnabled() const { return m_introEnabled; }
    void setIntroEnabled(bool on);
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
    QColor artColor() const { return m_artColor; }
    bool hasArtColor() const { return m_artColor.isValid(); }
    QColor artSecondary() const { return m_artSecondary; }
    QColor artAmbient() const { return m_artAmbient; }
    // The cover saved on disk, for desktop widgets that cannot fetch https art.
    QString artworkLocal() const { return m_artworkLocal; }
    // 0 off, 1 repeat the song, 2 repeat the queue (MusicKit's numbering).
    int repeatMode() const { return m_repeat; }
    bool shuffle() const { return m_shuffle; }
    bool muted() const { return m_volume < 0.001 && m_volumeBeforeMute > 0; }
    // Sets up a state for screenshots and tests: CLICK_PREVIEW_PANEL=sound, palette, heart, desktop, styles, scrub, morph or tray.
    QString previewPanel() const { return m_preview ? qEnvironmentVariable("CLICK_PREVIEW_PANEL") : QString(); }
    // A lively accent taken from a cover; invalid for grey or empty covers.
    static QColor accentFrom(const QImage &cover);
    // Three colours for the backdrop: the accent, a second hue from the cover
    // (or a neighbour of the accent), and a dark ambient tint. Empty for grey covers.
    static QVector<QColor> paletteFrom(const QImage &cover);
    bool mixAvailable() const;
    // While a song plays and nothing is queued behind it, Grok picks one very
    // similar song and it is queued next. Off until turned on: it sends the
    // song's title and artist to xAI.
    bool autoSimilar() const { return m_autoSimilar; }
    void setAutoSimilar(bool on);
    bool similarAvailable() const;
    QString similarStatus() const { return m_similarStatus; }
    // Test hooks: which program stands in for Grok, and how long a song plays before it is asked.
    void setSimilarProgram(const QString &program);
    void setMixProgram(const QString &program);
    void setSimilarDelay(int milliseconds) { m_similarDelay = milliseconds; m_similarTimer.setInterval(milliseconds); m_similarRecheck = qMax(20, milliseconds / 2); }

    // What Grok has been told about this listener (see taste.h), and rating it.
    bool useTaste() const { return m_useTaste; }
    void setUseTaste(bool on);
    QString tasteSummary() const { return m_taste->summary(); }
    QVariantList discoveries() const { return discoveriesAt(QTime::currentTime().hour()); }
    QVariantList discoveriesAt(int hour) const;
    int songRating() const { return m_taste->songRating(m_track, m_artist); } // 0 when not rated
    Taste *taste() const { return m_taste; }
    Stats *stats() const { return m_stats; }
    QObject *statsObject() const { return m_stats; }
    QStringList searchHistory() const { return m_history; }
    Q_INVOKABLE void rememberSearch(const QString &text);
    Q_INVOKABLE void forgetSearch(const QString &text = QString());
    Q_INVOKABLE void forgetStats() { m_stats->forget(); }
    // The heart: 1 to 10 for the song that is playing (tapping it gives an 8), 0 takes a rating back.
    // Not hearting a song says nothing: it may just be one they enjoy less than the others.
    Q_INVOKABLE void rateSong(int rating);
    QVariantList playlists() const { return m_playlists; }
    bool playlistsLoading() const { return m_playlistsLoading; }
    QString currentPlaylist() const { return m_currentPlaylistName; }
    bool liked() const { return m_likedId == m_trackId && !m_trackId.isEmpty() && m_liked; }
    QObject *lyricsObject() const { return m_lyrics; }
    Lyrics *lyricsModel() const { return m_lyrics; }
    bool wantLyrics() const { return m_wantLyrics; }
    void setWantLyrics(bool on);
    Q_INVOKABLE void refreshPlaylists();
    Q_INVOKABLE void addToPlaylist(const QString &playlistId, const QString &name);
    Q_INVOKABLE void addToCurrentPlaylist();
    // Makes a playlist holding the song that is playing, named `name` (or a suggested name when empty).
    Q_INVOKABLE void createPlaylist(const QString &name);
    // Flips the Apple Music heart, and the taste rating too when learning is on.
    Q_INVOKABLE void toggleLike();
    // Lowercase, two to four words, in the spirit of "3am rain & neon"; `seed` varies the pick per song.
    Q_INVOKABLE QString suggestPlaylistName(const QString &seed = QString()) const;
    static QString suggestPlaylistName(int hour, const QString &seed);
    // One sentence about the mood of a playlist, from its name.
    static QString playlistSummary(const QString &name);
    // Forces a saved name into the house style: lowercase, at most four words.
    static QString tidyPlaylistName(const QString &name);
    Q_INVOKABLE void forgetTaste();
    bool mixBusy() const { return m_mixBusy; }
    QString mixStatus() const { return m_mixStatus; }
    bool mixCanSave() const { return !m_mixIds.isEmpty() && !m_mixSaved; }
    QString mixPreview() const { return m_mixPreview; }
    QVariantList mixSongs() const { return m_mixSongs; }
    // Moods that fit this hour and day, worked out fresh on every read.
    QStringList mixChips() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void wheel(int steps);
    Q_INVOKABLE void center();
    Q_INVOKABLE void menu();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void nextTrack();
    Q_INVOKABLE void previousTrack();
    Q_INVOKABLE void nudgeVolume(int steps);
    Q_INVOKABLE void setVolume(double value);
    void applyVolume(double value, int rampMs);
    void setCurrentPlaylist(const QString &id, const QString &name);
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekBy(double seconds);
    Q_INVOKABLE QString formatClock(double seconds) const { return formatTime(seconds); }
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE void setRepeatMode(int mode);
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void setShuffle(bool on);
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
    void playlistsChanged();
    void wantLyricsChanged();
    void likedChanged();
    void similarChanged();
    void historyChanged();
    void introChanged();
    void tasteChanged();
    void artColorChanged();

private:
    struct Row {
        QString label;
        QString detail;
        QString action;
        QString type;
        QString id;           // what the action needs: an Apple Music id, or the path of the screen to open
        QString album;        // set on a track listed inside an album: play the album from here
        int position = -1;
        bool mine = false;    // a playlist the person manages (not one of Apple's)
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
    void fetchArtColor();
    void trackChangedForSimilar();
    void askForSimilar();
    void setSimilarStatus(const QString &status);
    void remember(const QString &title, const QString &artist);
    void retrySimilar(const QString &why);
    void waitForSimilar(const QString &why);
    void syncAutoplay();
    QString tasteNote(const QString &request) const;
    QString mixFor(const QString &title, const QString &artist) const;
    QVariantList withoutAvoided(const QVariantList &songs) const;
    static QString songKey(const QString &title, const QString &artist);
    void setPalette(const QVector<QColor> &palette);
    void cacheCover(const QByteArray &data, const QString &source);
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
    QColor m_artColor;
    QColor m_artSecondary;
    QColor m_artAmbient;
    QString m_artworkLocal;
    int m_repeat = 0;
    bool m_shuffle = false;
    double m_volumeBeforeMute = 0;
    QElapsedTimer m_volumeSetAt;
    double m_furthest = 0; // the furthest this song has played, as a fraction (not fooled by the next song's position arriving early)
    QString m_mixPreview;
    QVariantList m_mixSongs;
    class Mixer *m_similar = nullptr;
    QTimer m_similarTimer;
    bool m_autoSimilar = true;
    QString m_similarStatus;
    QString m_similarTitle;
    int m_upcoming = -1;
    int m_queueLength = -1;
    int m_queuePosition = -1;
    QStringList m_recent;
    QStringList m_autoAdded;      // songs this feature queued, so the chain knows where it is
    QStringList m_autoAddedIds;
    QStringList m_autoAddedTitles;
    static QString looseTitle(const QString &title);   // the same, by Apple Music id (names can differ a little between screens)
    QVariantList m_lastCandidates;
    bool m_similarAsked = false;  // a request is out (to Grok or the page)
    bool m_similarDone = false;   // (unused by the refill; kept for the skip guard)
    bool m_chainOn = false;       // the music is a pick and Grok's own follow-ups, not a playlist: keep the queue topped up
    QString m_chainTailTitle, m_chainTailArtist; // the last song Grok queued: the next ones are asked "like this"
    static constexpr int kQueueTarget = 10;
    int m_similarTries = 0;
    int m_similarChecks = 0;
    int m_similarRecheck = 4000;
    int m_similarDelay = 1500;  // how long a song plays before it is asked about (a re-check never changes it)

    Taste *m_taste = nullptr;
    Stats *m_stats = nullptr;
    QStringList m_history;
    bool m_sessionKnown = false;
    bool m_introEnabled = true;
    void markSessionKnown();
    QTimer m_historyTimer;
    QString m_historyPending;
    // What was heard of this song so far (for the play count), and whether it has been counted.
    double m_heard = 0;
    bool m_heardCounted = false;
    // Where the last session left off, brought back once after sign-in (see saveSession / restoreSession).
    bool m_restored = false;
    QTimer m_sessionTimer;
    QElapsedTimer m_restoreClock;
    void saveSession();
    void restoreSession();
    // A mix starts with its first song while Grok is still writing the rest.
    bool m_mixFirstSent = false;
    bool m_mixFirstPending = false;
    QVariantMap m_mixFirst;
    QVariantList m_mixRest;
    bool m_mixFinished = false;
    void sendMixRest();
    bool m_useTaste = true;
    QString m_mixRequest;
    class QNetworkAccessManager *m_net = nullptr;
    class Mixer *m_mixer = nullptr;
    bool m_mixBusy = false;
    bool m_mixSaved = false;
    QString m_mixStatus;
    Lyrics *m_lyrics = nullptr;
    bool m_wantLyrics = false;
    QString m_lyricsFor;   // the track id the lyrics on hand (or being fetched) belong to
    void fetchLyrics();
    QVariantList m_playlists;
    bool m_playlistsLoading = false;
    QString m_currentPlaylistId;
    QString m_currentPlaylistName;
    QString m_likedId;
    bool m_liked = false;
    QString m_mixName;
    QStringList m_mixIds;
    int m_serial = 0;
    int m_activeToken = 0;
};
