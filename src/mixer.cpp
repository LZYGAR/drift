#include "mixer.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

QString Mixer::buildPrompt(const QString &request, int count) {
    return QStringLiteral(
               "Make a playlist of %1 real, well-known songs that exist on Apple Music for this request: \"%2\". "
               "Mix familiar and less obvious picks, no duplicates. Reply with ONLY a JSON object, no prose and no "
               "code fences: {\"name\": \"short playlist name\", \"songs\": [{\"title\": \"...\", \"artist\": \"...\"}]}")
        .arg(count)
        .arg(request);
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
        name = QStringLiteral("Click Mix");
    return !songs.isEmpty();
}

void Mixer::generate(const QString &request, int count) {
    const QString text = cleanRequest(request);
    if (text.isEmpty() || m_process)
        return;
    if (!available()) {
        emit failed(tr("Grok is not installed."));
        return;
    }
    m_prompt = buildPrompt(text, qBound(5, count, kMaxSongs));
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
    const QByteArray out = process->readAllStandardOutput();
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
