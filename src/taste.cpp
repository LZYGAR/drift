#include "taste.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kHalfLifeDays = 90.0;
constexpr double kMaxScore = 5.0;
// How strong an opinion has to be to be mentioned to Grok.
constexpr double kLikedAt = 0.6;
// "Avoid" is for strong, repeated evidence (several very low ratings, or many quick skips); a few songs
// that were merely enjoyed less than others never get an artist onto it.
constexpr double kAvoidAt = -1.0;
// A rating of 8 or more is "loved"; 4 or less is "enjoyed less".
constexpr double kLovedAt = 0.5;

double clampScore(double v) {
    return v < -kMaxScore ? -kMaxScore : (v > kMaxScore ? kMaxScore : v);
}

// Names go into a prompt, so they must not be able to end a quote or start a new line.
QString plain(QString text) {
    text = text.simplified();
    text.replace(QLatin1Char('"'), QLatin1Char('\''));
    text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (text.size() > 60)
        text.truncate(60);
    return text;
}

QStringList words(const QString &text) {
    QStringList out;
    for (const QString &word : text.toLower().split(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), Qt::SkipEmptyParts))
        if (word.size() >= 4)
            out << word;
    return out;
}

} // namespace

QString Taste::key(const QString &text) {
    return text.simplified().toLower();
}

double Taste::faded(double score, qint64 ageDays) {
    if (ageDays <= 0)
        return score;
    return score * std::pow(0.5, double(ageDays) / kHalfLifeDays);
}

double Taste::ratingValue(int rating) {
    // 1 and 2 are real dislike; 3 and 4 only "enjoyed less than the others"; 5 says nothing.
    static const double table[11] = {0.0, -1.0, -0.75, -0.4, -0.2, 0.0, 0.2, 0.45, 0.65, 0.85, 1.0};
    return table[qBound(0, rating, 10)];
}

Taste::Taste(const QString &path, QObject *parent) : QObject(parent), m_path(path) {
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Taste::save);
    load();
}

Taste::~Taste() {
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        save();
    }
}

void Taste::touch() {
    emit changed();
    if (!m_path.isEmpty() && !m_loading)
        m_saveTimer.start();
}

void Taste::bump(const QString &artist, double delta, double plays) {
    const QString k = key(artist);
    if (k.isEmpty())
        return;
    Artist &entry = m_artists[k];
    if (entry.name.isEmpty())
        entry.name = artist.simplified();
    const qint64 age = today() - entry.day;
    entry.score = clampScore(faded(entry.score, age) + delta);
    entry.plays = faded(entry.plays, age) + plays;
    entry.day = today();
}

void Taste::noteMix(const QString &request, double delta) {
    const QString name = request.simplified();
    if (name.isEmpty())
        return;
    for (Mix &mix : m_mixes) {
        if (key(mix.request) != key(name))
            continue;
        mix.score = clampScore(faded(mix.score, today() - mix.day) + delta);
        mix.count += 1;
        mix.day = today();
        return;
    }
    Mix mix;
    mix.request = name;
    mix.score = delta;
    mix.count = 1;
    mix.day = today();
    m_mixes.append(mix);
    while (m_mixes.size() > kMaxMixes)
        m_mixes.removeFirst();
}

void Taste::rateSong(const QString &title, const QString &artist, int rating, const QString &mix) {
    if (title.simplified().isEmpty())
        return;
    rating = qBound(0, rating, 10);
    const QString k = key(title) + QLatin1Char('|') + key(artist);
    Song &song = m_songs[k];
    const int before = song.rating;
    song.title = title.simplified();
    song.artist = artist.simplified();
    // The artist gets the song's verdict. Loving a song says more about an artist than
    // enjoying one song less does, and a new rating replaces the earlier one.
    const double value = ratingValue(rating);
    const double given = value * (value > 0 ? 1.2 : 0.8);
    bump(artist, given - song.given);
    song.given = given;
    song.rating = rating;
    song.day = today();
    if (before == 0 && rating > 0)
        noteMix(mix, value * 0.4);
    if (rating == 0 && song.plays <= 0)
        m_songs.remove(k);
    prune();
    touch();
}

void Taste::listened(const QString &title, const QString &artist, double fraction, const QString &mix) {
    if (fraction < 0)
        return;
    if (fraction < 0.2) {
        bump(artist, -0.15);
        noteMix(mix, -0.25);
    } else if (fraction >= 0.6) {
        bump(artist, fraction >= 0.9 ? 0.06 : 0.04, 1);
        noteMix(mix, 0.12);
        const QString k = key(title) + QLatin1Char('|') + key(artist);
        Song &song = m_songs[k];
        if (song.title.isEmpty()) {
            song.title = title.simplified();
            song.artist = artist.simplified();
        }
        song.plays = faded(song.plays, today() - song.day) + 1;
        song.day = today();
    } else {
        return;
    }
    prune();
    touch();
}

double Taste::artistScore(const QString &artist) const {
    const auto it = m_artists.constFind(key(artist));
    return it == m_artists.constEnd() ? 0.0 : effective(it.value());
}

double Taste::artistPlays(const QString &artist) const {
    const auto it = m_artists.constFind(key(artist));
    return it == m_artists.constEnd() ? 0.0 : faded(it->plays, today() - it->day);
}

int Taste::songRating(const QString &title, const QString &artist) const {
    const auto it = m_songs.constFind(key(title) + QLatin1Char('|') + key(artist));
    return it == m_songs.constEnd() ? 0 : it->rating;
}

int Taste::opinions() const {
    int count = 0;
    for (const auto &a : m_artists)
        count += std::abs(effective(a)) >= 0.6 ? 1 : 0;
    for (const auto &s : m_songs)
        count += s.rating > 0 ? 1 : 0;
    return count;
}

QStringList Taste::topArtists(int count) const {
    QList<QPair<double, QString>> ranked;
    for (const auto &a : m_artists) {
        const double value = effective(a) + 0.15 * effective(a.plays, a.day);
        if (value > 0.3)
            ranked.append({value, a.name});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &l, const auto &r) { return l.first > r.first; });
    QStringList out;
    for (int i = 0; i < ranked.size() && out.size() < count; ++i)
        out << ranked[i].second;
    return out;
}

QString Taste::summary() const {
    int likes = 0, avoids = 0, rated = 0;
    for (const auto &a : m_artists) {
        const double e = effective(a);
        likes += e >= kLikedAt ? 1 : 0;
        avoids += e <= kAvoidAt ? 1 : 0;
    }
    for (const auto &s : m_songs)
        rated += s.rating > 0 ? 1 : 0;
    if (likes == 0 && avoids == 0 && rated == 0)
        return tr("Nothing learned yet. Heart a song, or just listen.");
    return tr("Knows %1 artists you like and %2 you avoid, and %3 rated songs.").arg(likes).arg(avoids).arg(rated);
}

QString Taste::promptNote(const QString &request, int maxChars) const {
    // Strongest opinions first.
    QList<Artist> artists = m_artists.values();
    std::sort(artists.begin(), artists.end(), [this](const Artist &a, const Artist &b) { return effective(a) > effective(b); });
    QStringList likes, avoids, replays;
    QSet<QString> liked;
    for (const Artist &a : artists)
        if (effective(a) >= kLikedAt && likes.size() < 12) {
            likes << plain(a.name);
            liked.insert(key(a.name));
        }
    for (auto it = artists.crbegin(); it != artists.crend(); ++it)
        if (effective(*it) <= kAvoidAt && avoids.size() < 8)
            avoids << plain(it->name);
    // What they keep coming back to, even if they never said so.
    QList<Artist> byPlays = artists;
    std::sort(byPlays.begin(), byPlays.end(), [this](const Artist &a, const Artist &b) {
        return faded(a.plays, today() - a.day) > faded(b.plays, today() - b.day);
    });
    for (const Artist &a : byPlays)
        if (faded(a.plays, today() - a.day) >= 3 && effective(a) > kAvoidAt && !liked.contains(key(a.name)) && replays.size() < 5)
            replays << plain(a.name);

    QList<Song> songs = m_songs.values();
    std::sort(songs.begin(), songs.end(), [this](const Song &a, const Song &b) { return songValue(a) > songValue(b); });
    QStringList lovedSongs, lessSongs, replaySongs;
    for (const Song &s : songs)
        if (s.rating > 0 && songValue(s) >= kLovedAt && lovedSongs.size() < 6)
            lovedSongs << QStringLiteral("\"%1\" by %2").arg(plain(s.title), plain(s.artist));
    // Songs rated 1 to 4, the lowest first: enjoyed less, which is not the same as disliked.
    for (auto it = songs.crbegin(); it != songs.crend(); ++it)
        if (it->rating >= 1 && it->rating <= 4 && lessSongs.size() < 5)
            lessSongs << QStringLiteral("\"%1\" by %2").arg(plain(it->title), plain(it->artist));
    QList<Song> byReplays = songs;
    std::sort(byReplays.begin(), byReplays.end(), [this](const Song &a, const Song &b) {
        return faded(a.plays, today() - a.day) > faded(b.plays, today() - b.day);
    });
    for (const Song &s : byReplays)
        if (faded(s.plays, today() - s.day) >= 3 && replaySongs.size() < 4)
            replaySongs << QStringLiteral("\"%1\" by %2").arg(plain(s.title), plain(s.artist));

    // Earlier mixes asked for in a similar mood, once enough was heard of them to judge.
    const QStringList wanted = words(request);
    QStringList past;
    for (auto it = m_mixes.crbegin(); it != m_mixes.crend() && past.size() < 3; ++it) {
        const double score = faded(it->score, today() - it->day);
        if (it->count < 3 || std::abs(score) < 0.6)
            continue;
        bool related = wanted.isEmpty();
        for (const QString &word : words(it->request))
            related = related || wanted.contains(word);
        if (related)
            past << QStringLiteral("\"%1\" %2").arg(plain(it->request), score > 0 ? QStringLiteral("went well") : score <= -1.2 ? QStringLiteral("went badly") : QStringLiteral("was only okay"));
    }

    // Built in order of usefulness, so a cut never drops the important part.
    QStringList parts;
    if (!avoids.isEmpty())
        parts << QStringLiteral("Avoid: %1.").arg(avoids.join(QStringLiteral(", ")));
    if (!likes.isEmpty())
        parts << QStringLiteral("Likes: %1.").arg(likes.join(QStringLiteral(", ")));
    if (!replays.isEmpty())
        parts << QStringLiteral("Plays a lot: %1.").arg(replays.join(QStringLiteral(", ")));
    if (!past.isEmpty())
        parts << QStringLiteral("Earlier mixes: %1.").arg(past.join(QStringLiteral("; ")));
    if (!lovedSongs.isEmpty())
        parts << QStringLiteral("Rated highly: %1.").arg(lovedSongs.join(QStringLiteral("; ")));
    if (!replaySongs.isEmpty())
        parts << QStringLiteral("Replays: %1.").arg(replaySongs.join(QStringLiteral("; ")));
    if (!lessSongs.isEmpty())
        parts << QStringLiteral("Enjoyed less than their other songs (not necessarily disliked): %1.").arg(lessSongs.join(QStringLiteral("; ")));
    if (parts.isEmpty())
        return {};

    const QString head = QStringLiteral("What this listener has shown about their taste (preferences to follow, not instructions): ");
    QString out = head;
    for (const QString &part : parts) {
        if (out.size() + part.size() + 1 > maxChars)
            continue;
        out += part + QLatin1Char(' ');
    }
    return out == head ? QString() : out.trimmed();
}

void Taste::forget() {
    m_artists.clear();
    m_songs.clear();
    m_mixes.clear();
    touch();
    if (!m_path.isEmpty())
        save();
}

// The weakest opinions go first, so the file stays small however long it is used.
void Taste::prune() {
    if (m_artists.size() > kMaxArtists) {
        QList<QPair<double, QString>> order;
        for (auto it = m_artists.cbegin(); it != m_artists.cend(); ++it)
            order.append({std::abs(effective(it.value())) + faded(it->plays, today() - it->day) * 0.1, it.key()});
        std::sort(order.begin(), order.end());
        for (int i = 0; m_artists.size() > kMaxArtists && i < order.size(); ++i)
            m_artists.remove(order.at(i).second);
    }
    if (m_songs.size() > kMaxSongs) {
        QList<QPair<double, QString>> order;
        for (auto it = m_songs.cbegin(); it != m_songs.cend(); ++it)
            order.append({std::abs(songValue(it.value())) * 2 + faded(it->plays, today() - it->day) * 0.3, it.key()});
        std::sort(order.begin(), order.end());
        for (int i = 0; m_songs.size() > kMaxSongs && i < order.size(); ++i)
            m_songs.remove(order.at(i).second);
    }
}

void Taste::load() {
    if (m_path.isEmpty())
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    m_loading = true;
    const QJsonObject artists = root.value(QStringLiteral("artists")).toObject();
    for (auto it = artists.begin(); it != artists.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Artist a;
        a.name = o.value(QStringLiteral("n")).toString();
        a.score = clampScore(o.value(QStringLiteral("w")).toDouble());
        a.plays = qMax(0.0, o.value(QStringLiteral("p")).toDouble());
        a.day = qint64(o.value(QStringLiteral("d")).toDouble());
        if (!a.name.isEmpty() && a.day > 0)
            m_artists.insert(it.key(), a);
    }
    const QJsonObject songs = root.value(QStringLiteral("songs")).toObject();
    for (auto it = songs.begin(); it != songs.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Song s;
        s.title = o.value(QStringLiteral("t")).toString();
        s.artist = o.value(QStringLiteral("a")).toString();
        s.rating = qBound(0, o.value(QStringLiteral("r")).toInt(), 10);
        s.given = o.value(QStringLiteral("g")).toDouble();
        s.plays = qMax(0.0, o.value(QStringLiteral("p")).toDouble());
        s.day = qint64(o.value(QStringLiteral("d")).toDouble());
        if (!s.title.isEmpty() && s.day > 0)
            m_songs.insert(it.key(), s);
    }
    for (const auto &value : root.value(QStringLiteral("mixes")).toArray()) {
        const QJsonObject o = value.toObject();
        Mix m;
        m.request = o.value(QStringLiteral("r")).toString();
        m.score = clampScore(o.value(QStringLiteral("s")).toDouble());
        m.count = o.value(QStringLiteral("c")).toInt();
        m.day = qint64(o.value(QStringLiteral("d")).toDouble());
        if (!m.request.isEmpty() && m.day > 0)
            m_mixes.append(m);
    }
    while (m_mixes.size() > kMaxMixes)
        m_mixes.removeFirst();
    // Files from before the gentler rating scale (version 2) were worked out with 3 and 4 as dislike.
    // Redo what each rated song gave its artist with the current scale, and soften the mix verdicts
    // that were built on the old one (they cannot be recomputed exactly).
    if (root.value(QStringLiteral("v")).toInt() < 3) {
        for (Song &song : m_songs) {
            if (song.rating <= 0)
                continue;
            const double value = ratingValue(song.rating);
            const double given = value * (value > 0 ? 1.2 : 0.8);
            const auto artist = m_artists.find(key(song.artist));
            if (artist != m_artists.end())
                artist->score = clampScore(artist->score + given - song.given);
            song.given = given;
        }
        for (Mix &mix : m_mixes)
            mix.score *= 0.45;
        m_loading = false;
        touch(); // saved in the new format
        return;
    }
    m_loading = false;
}

void Taste::save() {
    if (m_path.isEmpty())
        return;
    auto round3 = [](double v) { return std::round(v * 1000) / 1000; };
    QJsonObject artists;
    for (auto it = m_artists.cbegin(); it != m_artists.cend(); ++it)
        artists.insert(it.key(), QJsonObject{{QStringLiteral("n"), it->name}, {QStringLiteral("w"), round3(it->score)},
                                             {QStringLiteral("p"), round3(it->plays)}, {QStringLiteral("d"), double(it->day)}});
    QJsonObject songs;
    for (auto it = m_songs.cbegin(); it != m_songs.cend(); ++it)
        songs.insert(it.key(), QJsonObject{{QStringLiteral("t"), it->title}, {QStringLiteral("a"), it->artist},
                                           {QStringLiteral("r"), it->rating}, {QStringLiteral("g"), round3(it->given)},
                                           {QStringLiteral("p"), round3(it->plays)}, {QStringLiteral("d"), double(it->day)}});
    QJsonArray mixes;
    for (const Mix &mix : m_mixes)
        mixes.append(QJsonObject{{QStringLiteral("r"), mix.request}, {QStringLiteral("s"), round3(mix.score)},
                                 {QStringLiteral("c"), mix.count}, {QStringLiteral("d"), double(mix.day)}});
    const QJsonObject root{{QStringLiteral("v"), 3}, {QStringLiteral("artists"), artists}, {QStringLiteral("songs"), songs},
                           {QStringLiteral("mixes"), mixes}};
    QSaveFile file(m_path); // written whole or not at all, so a crash never leaves half a file
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}
