#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVector>

// Asks the Grok CLI to pick songs for a described mood. Grok only ever sees
// the sentence the user typed, runs in an empty folder, and has every tool
// denied, so all it can do is answer with text.
class Mixer : public QObject {
    Q_OBJECT
public:
    explicit Mixer(QObject *parent = nullptr);
    ~Mixer() override;

    bool available() const;
    bool busy() const { return m_process != nullptr; }
    void setProgram(const QString &program) { m_program = program; }
    // `taste` is the listener's profile (see taste.h), added to the prompt when given.
    void generate(const QString &request, int count = 20, const QString &taste = {});
    // The fast model at low effort answers in about 6 s; the default takes 30 to 50.
    static QStringList arguments(const QString &prompt, bool fast);
    void cancel();

    static QString buildPrompt(const QString &request, int count, const QString &taste = {});
    // Asks for one song that sounds very much like the one that is playing, and
    // is not one of `avoid` ("Title\nArtist" entries). Answers like generate().
    void similar(const QString &title, const QString &artist, const QStringList &avoid, int count = 3, const QString &taste = {});
    static QString buildSimilarPrompt(const QString &title, const QString &artist, const QStringList &avoid, int count = 3,
                                      const QString &taste = {});
    // Pulls the JSON object out of Grok's reply even if it added prose or code fences.
    static bool parse(const QByteArray &output, QString &name, QVector<QPair<QString, QString>> &songs);
    static QString cleanRequest(const QString &request);
    // Songs that are already complete in a reply that is still being written.
    static QVariantList partialSongs(const QByteArray &output);
    // One-tap moods that fit the moment: the hour and the day of the week.
    static QStringList contextChips(const QDateTime &now);

signals:
    // While Grok writes: the last stretch of raw text, and the songs so far.
    void partial(const QString &tail, const QVariantList &songs);
    void finished(const QString &name, const QVariantList &songs);
    void failed(const QString &message);

private:
    void launch(const QString &prompt, bool fast);
    void done(int code, QProcess::ExitStatus status);

    QString m_program = QStringLiteral("grok");
    QProcess *m_process = nullptr;
    QString m_prompt;
    QByteArray m_output;
    bool m_triedFast = false;
    QTimer m_timeout;
};
