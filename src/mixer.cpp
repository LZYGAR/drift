#include "mixer.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {
constexpr int kMaxSongs = 40;
constexpr int kMaxRequest = 200;
}

Mixer::Mixer(QObject *parent) : QObject(parent) {
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(90000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (!m_process)
            return;
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        process->kill();
        process->deleteLater();
        emit failed(tr("Grok took too long. Try again."));
    });
}

Mixer::~Mixer() {
    cancel();
}

bool Mixer::available() const {
    return !QStandardPaths::findExecutable(m_program).isEmpty();
}

QString Mixer::cleanRequest(const QString &request) {
    QString text = request.simplified();
    // Keep it a short sentence: it is the only thing that leaves this machine.
    if (text.size() > kMaxRequest)
        text.truncate(kMaxRequest);
    return text;
}

QString Mixer::buildPrompt(const QString &request, int count, const QString &taste) {
    return QStringLiteral(
               "Make a playlist of %1 real, well-known songs that exist on Apple Music for this request: \"%2\". "
               "Mix familiar and less obvious picks, no duplicates. %3Reply with ONLY a JSON object, no prose and no "
               "code fences: {\"name\": \"short playlist name\", \"songs\": [{\"title\": \"...\", \"artist\": \"...\"}]}")
        .arg(count)
        .arg(request)
        .arg(taste.isEmpty() ? QString() : taste + QLatin1Char(' '));
}

bool Mixer::parse(const QByteArray &output, QString &name, QVector<QPair<QString, QString>> &songs) {
    const int start = output.indexOf('{');
    const int end = output.lastIndexOf('}');
    if (start < 0 || end <= start)
        return false;
    const QJsonObject object = QJsonDocument::fromJson(output.mid(start, end - start + 1)).object();
    const QJsonArray list = object.value(QStringLiteral("songs")).toArray();
    songs.clear();
    QSet<QString> seen;
    for (const auto &value : list) {
        const QJsonObject song = value.toObject();
        const QString title = song.value(QStringLiteral("title")).toString().simplified();
        const QString artist = song.value(QStringLiteral("artist")).toString().simplified();
        if (title.isEmpty() || artist.isEmpty())
            continue;
        const QString key = (title + QLatin1Char('|') + artist).toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        songs.append({title, artist});
        if (songs.size() >= kMaxSongs)
            break;
    }
    name = object.value(QStringLiteral("name")).toString().simplified();
    if (name.isEmpty())
        name = QStringLiteral("Drift Mix");
    return !songs.isEmpty();
}

QVariantList Mixer::partialSongs(const QByteArray &output) {
    static const QRegularExpression pair(
        QStringLiteral(R"re("title"\s*:\s*"((?:[^"\\]|\\.)+)"\s*,\s*"artist"\s*:\s*"((?:[^"\\]|\\.)+)")re"));
    QVariantList songs;
    auto matches = pair.globalMatch(QString::fromUtf8(output));
    while (matches.hasNext() && songs.size() < kMaxSongs) {
        const auto match = matches.next();
        auto plain = [](QString text) { return text.replace(QStringLiteral("\\\""), QStringLiteral("\"")).simplified(); };
        songs.append(QVariantMap{{QStringLiteral("title"), plain(match.captured(1))},
                                 {QStringLiteral("artist"), plain(match.captured(2))}});
    }
    return songs;
}

QStringList Mixer::contextChips(const QDateTime &now) {
    const int hour = now.time().hour();
    const int day = now.date().dayOfWeek(); // 1 is Monday
    const bool weekend = day >= 6;
    const QString dayName = QLocale::c().dayName(day, QLocale::LongFormat);
    QStringList chips;
    if (hour < 5) {
        chips << tr("Midnight code session") << tr("After hours") << tr("Can't sleep");
    } else if (hour < 9) {
        chips << (weekend ? tr("Slow %1 morning").arg(dayName) : tr("%1 morning coffee").arg(dayName))
              << tr("Sunrise run") << tr("Easy start");
    } else if (hour < 12) {
        chips << (weekend ? tr("Lazy %1 brunch").arg(dayName) : tr("Deep focus morning"))
              << tr("Bright and upbeat") << tr("Instrumental flow");
    } else if (hour < 17) {
        chips << (weekend ? tr("%1 afternoon outside").arg(dayName) : tr("Afternoon slump fix"))
              << tr("Heads-down work") << tr("Feel-good classics");
    } else if (hour < 21) {
        chips << (day == 5 ? tr("Friday night warm-up") : weekend ? tr("%1 night in").arg(dayName) : tr("After work unwind"))
              << tr("Cooking dinner") << tr("Golden hour");
    } else {
        chips << (day == 5 || day == 6 ? tr("Late night out") : tr("Late night wind-down"))
              << tr("Night drive") << tr("Quiet and slow");
    }
    return chips;
}

namespace {
// A title or artist is quoted inside a prompt, so it must not be able to close the quote or start a new line.
QString quotable(QString text) {
    text = text.simplified();
    text.replace(QLatin1Char('"'), QLatin1Char('\''));
    text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (text.size() > 100)
        text.truncate(100);
    return text;
}
} // namespace

QString Mixer::buildSimilarPrompt(const QString &title, const QString &artist, const QStringList &avoid, int count,
                                  const QString &taste) {
    count = qBound(1, count, 12);
    QStringList skip;
    for (const QString &entry : avoid.mid(qMax(0, avoid.size() - 40))) {
        const QStringList parts = entry.split(QLatin1Char('\n'));
        skip << QStringLiteral("\"%1\" by %2").arg(quotable(parts.value(0)), quotable(parts.value(1)));
    }
    QString prompt = QStringLiteral(
                         "Pick %1 different real song(s) on Apple Music that are very similar to \"%2\" by %3: the same sound, mood, "
                         "tempo and era, things a fan of it would love to hear next, best match first. None may be that song. ")
                         .arg(count)
                         .arg(quotable(title), quotable(artist));
    if (!skip.isEmpty())
        prompt += QStringLiteral("It must not be any of these: %1. ").arg(skip.join(QStringLiteral("; ")));
    if (!taste.isEmpty())
        prompt += taste + QLatin1Char(' ');
    prompt += QStringLiteral(
        "Reply with ONLY a JSON object, no prose and no code fences: "
        "{\"name\": \"Similar\", \"songs\": [{\"title\": \"...\", \"artist\": \"...\"}]}");
    return prompt;
}

void Mixer::similar(const QString &title, const QString &artist, const QStringList &avoid, int count, const QString &taste) {
    if (title.simplified().isEmpty() || m_process)
        return;
    if (!available()) {
        emit failed(tr("Grok is not installed."));
        return;
    }
    m_prompt = buildSimilarPrompt(title, artist, avoid, count, taste);
    launch(m_prompt, true);
}

void Mixer::generate(const QString &request, int count, const QString &taste) {
    const QString text = cleanRequest(request);
    if (text.isEmpty() || m_process)
        return;
    if (!available()) {
        emit failed(tr("Grok is not installed."));
        return;
    }
    m_prompt = buildPrompt(text, qBound(5, count, kMaxSongs), taste);
    launch(m_prompt, true);
}

QStringList Mixer::arguments(const QString &prompt, bool fast) {
    QStringList args{QStringLiteral("--permission-mode"), QStringLiteral("dontAsk"),
                     QStringLiteral("--disable-web-search"), QStringLiteral("--no-subagents"),
                     QStringLiteral("--max-turns"), QStringLiteral("2"),
                     QStringLiteral("--output-format"), QStringLiteral("plain")};
    if (fast)
        args << QStringLiteral("-m") << QStringLiteral("grok-4.7-build-fast") << QStringLiteral("--effort")
             << QStringLiteral("low");
    args << QStringLiteral("-p") << prompt;
    return args;
}

void Mixer::launch(const QString &prompt, bool fast) {
    // An empty folder, so the agent has nothing of yours to look at.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/mix");
    QDir().mkpath(dir);

    m_triedFast = fast;
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(dir);
    m_process->setStandardErrorFile(QProcess::nullDevice());
    m_output.clear();
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        if (!m_process)
            return;
        m_output += m_process->readAllStandardOutput();
        // Just the end of the stream, flattened to one line, as a sign of life.
        const QString tail = QString::fromUtf8(m_output.right(240)).simplified().right(110);
        emit partial(tail, partialSongs(m_output));
    });
    connect(m_process, &QProcess::finished, this, &Mixer::done);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        m_timeout.stop();
        emit failed(tr("Could not start Grok."));
    });
    m_process->start(m_program, arguments(prompt, fast));
    m_timeout.start();
}

void Mixer::cancel() {
    m_timeout.stop();
    if (!m_process)
        return;
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->kill();
    process->deleteLater();
}

void Mixer::done(int code, QProcess::ExitStatus status) {
    m_timeout.stop();
    QProcess *process = m_process;
    m_process = nullptr;
    if (!process)
        return;
    const QByteArray out = m_output + process->readAllStandardOutput();
    m_output.clear();
    process->deleteLater();
    QString name;
    QVector<QPair<QString, QString>> songs;
    if (status != QProcess::NormalExit || code != 0 || !parse(out, name, songs)) {
        // If the fast model was unavailable, try once more on Grok's defaults.
        if (m_triedFast) {
            launch(m_prompt, false);
            return;
        }
        emit failed(tr("Grok did not return a playlist. Try rewording."));
        return;
    }
    QVariantList list;
    for (const auto &song : songs)
        list.append(QVariantMap{{QStringLiteral("title"), song.first}, {QStringLiteral("artist"), song.second}});
    emit finished(name, list);
}
