#pragma once

#include <QDate>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTimer>

// What the listener likes and does not, learned quietly, so Grok's mixes and
// suggestions get better the more it is used.
//
// Three kinds of signal, none of them a chore:
//   a heart       1 to 10 on a song. Tapping the heart is an 8; hovering it
//                 offers the whole scale. Not hearting a song means nothing:
//                 it may simply be one you enjoy less than the others.
//   listening     a song played most of the way counts a little for its artist
//                 and is counted as a play; one skipped within the first fifth
//                 counts a little against.
//   mixes         the same signals, also credited to the mood a mix was asked for.
//
// It is kept small on purpose: one JSON file (a few KB) of running totals, not
// a log of everything played. A score fades with a half-life of 90 days, so
// last year's favourites count for less than last week's, and each map has a hard
// size cap (the weakest opinions are dropped first). Scores run from about -5
// (avoid) to +5 (love). What Grok is sent is a plain-text summary of at most ~700
// characters, listing only the clear opinions.
class Taste : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(int opinions READ opinions NOTIFY changed)

public:
    // An empty path keeps everything in memory (used by tests).
    explicit Taste(const QString &path = {}, QObject *parent = nullptr);
    ~Taste() override;

    // `rating` is 1 to 10, or 0 to take a rating back. `mix` is the mood of the
    // mix the song came from, if it came from one.
    void rateSong(const QString &title, const QString &artist, int rating, const QString &mix = {});
    int songRating(const QString &title, const QString &artist) const; // 0 when not rated
    // A song just ended (or was skipped) after `fraction` of its length was played.
    void listened(const QString &title, const QString &artist, double fraction, const QString &mix = {});

    double artistScore(const QString &artist) const;
    bool avoids(const QString &artist) const { return artistScore(artist) <= -1.5; }
    // How many times the songs of an artist were played through (fading with time).
    double artistPlays(const QString &artist) const;

    // The profile for a prompt. `request` is the mood a mix is for, so earlier
    // mixes with a similar mood come first.
    QString promptNote(const QString &request = {}, int maxChars = 700) const;
    QString summary() const;
    // The artists liked most right now (score and plays together), best first.
    QStringList topArtists(int count) const;
    int opinions() const; // artists and songs with a clear opinion
    void forget();

    // Test hooks and arithmetic.
    void setToday(qint64 julianDay) { m_today = julianDay; }
    static double faded(double score, qint64 ageDays);
    // 1..10 mapped to -1..1. The middle says little, and a low rating (3 or 4) only means "enjoyed less",
    // not "dislike": only 1 and 2 count as real dislike.
    static double ratingValue(int rating);
    static QString key(const QString &text);
    static constexpr int kMaxArtists = 200;
    static constexpr int kMaxSongs = 150;
    static constexpr int kMaxMixes = 40;

signals:
    void changed();

private:
    struct Artist {
        QString name;
        double score = 0;
        double plays = 0;
        qint64 day = 0;
    };
    struct Song {
        QString title;
        QString artist;
        int rating = 0;
        double given = 0; // what this song's rating added to its artist, so a new rating can replace it
        double plays = 0;
        qint64 day = 0;
    };
    struct Mix {
        QString request;
        double score = 0;
        int count = 0;
        qint64 day = 0;
    };

    qint64 today() const { return m_today > 0 ? m_today : QDate::currentDate().toJulianDay(); }
    double effective(double value, qint64 day) const { return faded(value, today() - day); }
    double effective(const Artist &a) const { return effective(a.score, a.day); }
    double songValue(const Song &s) const { return s.rating > 0 ? effective(ratingValue(s.rating), s.day) : 0.0; }
    void bump(const QString &artist, double delta, double plays = 0);
    void noteMix(const QString &request, double delta);
    void prune();
    void load();
    void save();
    void touch();

    QString m_path;
    QHash<QString, Artist> m_artists;
    QHash<QString, Song> m_songs;
    QList<Mix> m_mixes;
    QTimer m_saveTimer;
    qint64 m_today = 0;
    bool m_loading = false;
};
