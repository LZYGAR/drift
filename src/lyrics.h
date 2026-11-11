#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

// The words of the song that is playing, and which line is being sung.
//
// Apple Music hands over TTML (line- or word-timed); LRC ("[01:23.45]words") and plain text are understood too.
// Whatever comes in is reduced to lines with a start time in milliseconds. Plain text has no times: `synced`
// is false and nothing is highlighted or seekable.
class Lyrics : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList lines READ lines NOTIFY changed)
    Q_PROPERTY(bool synced READ synced NOTIFY changed)
    Q_PROPERTY(bool hasLyrics READ hasLyrics NOTIFY changed)
    // "idle", "loading", "ready" or "none" (this song has no words, or they could not be read).
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    // One word of a line, with the time it starts being sung and ends (-1 when unknown).
    struct Word {
        qint64 startMs = -1;
        qint64 endMs = -1;
        QString text;
    };
    struct Line {
        qint64 startMs = -1; // -1 when the line has no time
        qint64 endMs = -1;
        QString text;
        QVector<Word> words; // from the file when it is word-timed, otherwise spread over the line (ensureWords)
    };

    using QObject::QObject;

    QVariantList lines() const;
    bool synced() const { return m_synced; }
    bool hasLyrics() const { return !m_lines.isEmpty(); }
    QString status() const { return m_status; }
    const QVector<Line> &all() const { return m_lines; }

    // Index of the line being sung at `seconds` (the last line that started), or -1 before the first.
    Q_INVOKABLE int indexAt(double seconds) const;
    Q_INVOKABLE qint64 startOf(int index) const;

    void setLoading();
    void setNone();
    // Takes TTML, LRC or plain text; returns false (and says "none") when nothing readable is in it.
    bool setText(const QString &text);
    void clear();

    // ---- pure parsers, exposed for tests ----
    // A TTML clock value in milliseconds: "1:23.456", "83.456s", "00:01:23.456", "1500ms", "12.5"; -1 if unreadable.
    static qint64 parseClock(const QString &value);
    static QVector<Line> parseTtml(const QString &ttml);
    static QVector<Line> parseLrc(const QString &lrc);
    static QVector<Line> parsePlain(const QString &text);
    // Gives every line its words. Lines that came word-timed keep their times; the rest are spread over the
    // line by length, so the word being sung can be shown for any song.
    static void ensureWords(QVector<Line> &lines);
    // Picks the right parser for what it is given.
    static QVector<Line> parseAny(const QString &text);

signals:
    void changed();

private:
    QVector<Line> m_lines;
    bool m_synced = false;
    QString m_status = QStringLiteral("idle");
};
