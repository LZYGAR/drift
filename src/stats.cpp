#include "stats.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace {
constexpr int maxSongs = 400;
constexpr int maxArtists = 200;
constexpr int maxDays = 400;
}

Stats::Stats(const QString &path, QObject *parent) : QObject(parent), m_path(path) {
    m_save.setSingleShot(true);
    m_save.setInterval(20000);
    connect(&m_save, &QTimer::timeout, this, &Stats::flush);
    // The card does not need to redraw ten times a second.
    m_notify.setSingleShot(true);
    m_notify.setInterval(1000);
    connect(&m_notify, &QTimer::timeout, this, &Stats::changed);
    load();
}

Stats::~Stats() {
    flush();
}

QString Stats::label(double seconds) {
    const int minutes = int(seconds / 60.0);
    if (minutes < 1)
        return QStringLiteral("%1 s").arg(int(seconds));
    if (minutes < 60)
        return QStringLiteral("%1 min").arg(minutes);
    const int hours = minutes / 60;
    if (hours >= 100)
        return QStringLiteral("%1 h").arg(hours);
    return QStringLiteral("%1 h %2 min").arg(hours).arg(minutes % 60);
}

void Stats::touch() {
    m_dirty = true;
    if (!m_save.isActive())
        m_save.start();
    if (!m_notify.isActive())
        m_notify.start();
}

void Stats::listen(const QString &key, const QString &title, const QString &artist, const QString &artwork, double seconds,
                   const QDateTime &when) {
    if (key.isEmpty() || seconds <= 0)
        return;
    Song &song = m_songs[key];
    song.title = title;
    song.artist = artist;
    if (!artwork.isEmpty())
        song.artwork = artwork;
    song.seconds += seconds;
    if (!artist.isEmpty())
        m_artists[artist] += seconds;
    m_days[when.date().toString(Qt::ISODate)] += seconds;
    m_hours[when.time().hour()] += seconds;
    m_total += seconds;
    touch();
}

void Stats::countPlay(const QString &key, const QString &title, const QString &artist, const QString &artwork) {
    if (key.isEmpty())
        return;
    Song &song = m_songs[key];
    song.title = title;
    song.artist = artist;
    if (!artwork.isEmpty())
        song.artwork = artwork;
    song.plays++;
    m_plays++;
    touch();
}

QString Stats::today() const {
    return label(m_days.value(QDate::currentDate().toString(Qt::ISODate)));
}

int Stats::streak(const QDate &today) const {
    int days = 0;
    QDate day = today;
    // Today may not have started yet: a streak holds until the day is over.
    if (m_days.value(day.toString(Qt::ISODate)) < 60)
        day = day.addDays(-1);
    while (m_days.value(day.toString(Qt::ISODate)) >= 60) {
        days++;
        day = day.addDays(-1);
    }
    return days;
}

QString Stats::peak() const {
    // Four parts of the day, whichever has the most listening.
    const struct { const char *name; int from, to; } parts[] = {{"mornings", 5, 12}, {"afternoons", 12, 17}, {"evenings", 17, 22}, {"late nights", 22, 29}};
    double best = 0;
    QString name;
    for (const auto &part : parts) {
        double sum = 0;
        for (int h = part.from; h < part.to; ++h)
            sum += m_hours[h % 24];
        if (sum > best) {
            best = sum;
            name = QString::fromLatin1(part.name);
        }
    }
    return name;
}

QVariantList Stats::week(const QDate &today) const {
    QVariantList list;
    for (int back = 6; back >= 0; --back) {
        const QDate day = today.addDays(-back);
        list.append(QVariantMap{{QStringLiteral("day"), QLocale().dayName(day.dayOfWeek(), QLocale::NarrowFormat)},
                                {QStringLiteral("seconds"), m_days.value(day.toString(Qt::ISODate))}});
    }
    return list;
}

QVariantList Stats::topSongs(int limit) const {
    QList<Song> songs = m_songs.values();
    std::sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) { return a.seconds > b.seconds; });
    QVariantList list;
    for (const Song &song : songs) {
        if (list.size() >= limit)
            break;
        list.append(QVariantMap{{QStringLiteral("title"), song.title}, {QStringLiteral("artist"), song.artist},
                                {QStringLiteral("artwork"), song.artwork}, {QStringLiteral("plays"), song.plays},
                                {QStringLiteral("time"), label(song.seconds)}, {QStringLiteral("seconds"), song.seconds}});
    }
    return list;
}

QVariantList Stats::topArtists(int limit) const {
    QList<QPair<QString, double>> artists;
    for (auto it = m_artists.constBegin(); it != m_artists.constEnd(); ++it)
        artists.append({it.key(), it.value()});
    std::sort(artists.begin(), artists.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    QVariantList list;
    for (const auto &artist : artists) {
        if (list.size() >= limit)
            break;
        list.append(QVariantMap{{QStringLiteral("name"), artist.first}, {QStringLiteral("time"), label(artist.second)},
                                {QStringLiteral("seconds"), artist.second}});
    }
    return list;
}

void Stats::forget() {
    m_songs.clear();
    m_artists.clear();
    m_days.clear();
    std::fill(std::begin(m_hours), std::end(m_hours), 0.0);
    m_total = 0;
    m_plays = 0;
    m_dirty = true;
    flush();
    emit changed();
}

void Stats::trim() {
    if (m_songs.size() > maxSongs) {
        QList<QPair<double, QString>> order;
        for (auto it = m_songs.constBegin(); it != m_songs.constEnd(); ++it)
            order.append({it.value().seconds, it.key()});
        std::sort(order.begin(), order.end());
        const int dropSongs = m_songs.size() - maxSongs;
        for (int i = 0; i < dropSongs; ++i)
            m_songs.remove(order[i].second);
    }
    if (m_artists.size() > maxArtists) {
        QList<QPair<double, QString>> order;
        for (auto it = m_artists.constBegin(); it != m_artists.constEnd(); ++it)
            order.append({it.value(), it.key()});
        std::sort(order.begin(), order.end());
        const int drop = m_artists.size() - maxArtists;
        for (int i = 0; i < drop; ++i)
            m_artists.remove(order[i].second);
    }
    if (m_days.size() > maxDays) {
        QStringList keys = m_days.keys();
        keys.sort();
        const int drop = keys.size() - maxDays;
        for (int i = 0; i < drop; ++i)
            m_days.remove(keys[i]);
    }
}

void Stats::flush() {
    m_save.stop();
    if (m_path.isEmpty() || !m_dirty)
        return;
    trim();
    QJsonObject songs;
    for (auto it = m_songs.constBegin(); it != m_songs.constEnd(); ++it)
        songs.insert(it.key(), QJsonObject{{"t", it.value().title}, {"a", it.value().artist}, {"w", it.value().artwork},
                                           {"s", it.value().seconds}, {"p", it.value().plays}});
    QJsonObject artists, days;
    for (auto it = m_artists.constBegin(); it != m_artists.constEnd(); ++it)
        artists.insert(it.key(), it.value());
    for (auto it = m_days.constBegin(); it != m_days.constEnd(); ++it)
        days.insert(it.key(), it.value());
    QJsonArray hours;
    for (double h : m_hours)
        hours.append(h);
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{"songs", songs}, {"artists", artists}, {"days", days}, {"hours", hours},
                                         {"total", m_total}, {"plays", m_plays}}).toJson(QJsonDocument::Compact));
    if (file.commit())
        m_dirty = false;
}

void Stats::load() {
    if (m_path.isEmpty())
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject songs = root.value("songs").toObject();
    for (auto it = songs.begin(); it != songs.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        Song song;
        song.title = o.value("t").toString();
        song.artist = o.value("a").toString();
        song.artwork = o.value("w").toString();
        song.seconds = o.value("s").toDouble();
        song.plays = o.value("p").toInt();
        m_songs.insert(it.key(), song);
    }
    const QJsonObject artists = root.value("artists").toObject();
    for (auto it = artists.begin(); it != artists.end(); ++it)
        m_artists.insert(it.key(), it.value().toDouble());
    const QJsonObject days = root.value("days").toObject();
    for (auto it = days.begin(); it != days.end(); ++it)
        m_days.insert(it.key(), it.value().toDouble());
    const QJsonArray hours = root.value("hours").toArray();
    for (int i = 0; i < 24 && i < hours.size(); ++i)
        m_hours[i] = hours.at(i).toDouble();
    m_total = root.value("total").toDouble();
    m_plays = root.value("plays").toInt();
}
