#pragma once

#include <QByteArray>
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
    void generate(const QString &request, int count = 20);
    // The fast model at low effort answers in about 6 s; the default takes 30 to 50.
    static QStringList arguments(const QString &prompt, bool fast);
    void cancel();

    static QString buildPrompt(const QString &request, int count);
    // Pulls the JSON object out of Grok's reply even if it added prose or code fences.
    static bool parse(const QByteArray &output, QString &name, QVector<QPair<QString, QString>> &songs);
    static QString cleanRequest(const QString &request);

signals:
    void finished(const QString &name, const QVariantList &songs);
    void failed(const QString &message);

private:
    void launch(const QString &prompt, bool fast);
    void done(int code, QProcess::ExitStatus status);

    QString m_program = QStringLiteral("grok");
    QProcess *m_process = nullptr;
    QString m_prompt;
    bool m_triedFast = false;
    QTimer m_timeout;
};
