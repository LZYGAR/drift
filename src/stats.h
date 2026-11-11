#pragma once

#include <QDate>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

// A quiet tally of what was listened to, for the "Wrapped" card in settings.
//
// One small JSON file of running totals (never a log of plays): seconds per song,
// per artist, per day and per hour of the day. Songs and artists are capped, the
// weakest dropped first, so it stays a few tens of KB however long it runs.
// Nothing here leaves the machine.
class Stats : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    Q_PROPERTY(QString total READ total NOTIFY changed)
    Q_PROPERTY(QString today READ today NOTIFY changed)
    Q_PROPERTY(int plays READ plays NOTIFY changed)
    Q_PROPERTY(int songCount READ songCount NOTIFY changed)
    Q_PROPERTY(int streak READ streak NOTIFY changed)
    Q_PROPERTY(QString peak READ peak NOTIFY changed)
    Q_PROPERTY(QVariantList week READ week NOTIFY changed)
    Q_PROPERTY(QVariantList topSongs READ topSongs NOTIFY changed)
    Q_PROPERTY(QVariantList topArtists READ topArtists NOTIFY changed)

public:
    // An empty path keeps everything in memory (used by tests).
    explicit Stats(const QString &path = {}, QObject *parent = nullptr);
    ~Stats() override;

    // `seconds` of one song heard now (or `when`, for tests).
    void listen(const QString &key, const QString &title, const QString &artist, const QString &artwork, double seconds,
                const QDateTime &when = QDateTime::currentDateTime());
    // The song has been heard long enough to count as a play.
    void countPlay(const QString &key, const QString &title, const QString &artist, const QString &artwork);
    void forget();
    void flush();

    bool hasData() const { return m_total > 1; }
    QString total() const { return label(m_total); }
    QString today() const;
    int plays() const { return m_plays; }
    int songCount() const { return m_songs.size(); }
    int streak(const QDate &today = QDate::currentDate()) const;
    QString peak() const;
    QVariantList week(const QDate &today = QDate::currentDate()) const; // 7 days, oldest first: {day, seconds}
    QVariantList topSongs(int limit = 5) const;
    QVariantList topArtists(int limit = 5) const;
    double totalSeconds() const { return m_total; }

    // "3 h 20 min", "45 min", "40 s".
    static QString label(double seconds);

signals:
    void changed();

private:
    struct Song {
        QString title, artist, artwork;
        double seconds = 0;
        int plays = 0;
    };
    void load();
    void trim();
    void touch();

    QString m_path;
    QHash<QString, Song> m_songs;
    QHash<QString, double> m_artists;
    QHash<QString, double> m_days;   // yyyy-MM-dd
    double m_hours[24] = {};
    double m_total = 0;
    int m_plays = 0;
    bool m_dirty = false;
    QTimer m_save;
    QTimer m_notify;
};
