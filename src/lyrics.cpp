#include "lyrics.h"

#include <QRegularExpression>
#include <QVariantMap>
#include <QXmlStreamReader>
#include <algorithm>

qint64 Lyrics::parseClock(const QString &raw) {
    QString value = raw.trimmed();
    if (value.isEmpty())
        return -1;
    double unit = 1000.0; // what a bare number means: seconds
    if (value.endsWith(QLatin1String("ms"))) {
        unit = 1.0;
        value.chop(2);
    } else if (value.endsWith(QLatin1Char('s'))) {
        value.chop(1);
    }
    const QStringList parts = value.split(QLatin1Char(':'));
    if (parts.size() > 3)
        return -1;
    double seconds = 0;
    for (const QString &part : parts) {
        bool ok = false;
        const double number = part.toDouble(&ok);
        if (!ok || number < 0)
            return -1;
        seconds = seconds * 60.0 + number;
    }
    // "ms" only applies to a bare number; with colons the last part is already seconds.
    return qRound64(parts.size() == 1 ? seconds * unit : seconds * 1000.0);
}

// Splits a line's characters into words, each timed from its first character to its last when the file
// gave the characters times (word-timed TTML), or untimed (-1) otherwise.
static QVector<Lyrics::Word> wordsFrom(const QString &raw, const QVector<QPair<qint64, qint64>> &times) {
    QVector<Lyrics::Word> words;
    Lyrics::Word current;
    bool open = false;
    for (int i = 0; i < raw.size(); ++i) {
        if (raw.at(i).isSpace()) {
            if (open) {
                words.append(current);
                current = Lyrics::Word();
                open = false;
            }
            continue;
        }
        const auto t = i < times.size() ? times.at(i) : qMakePair(qint64(-1), qint64(-1));
        if (!open) {
            current.startMs = t.first;
            open = true;
        }
        if (t.second >= 0)
            current.endMs = t.second;
        current.text += raw.at(i);
    }
    if (open)
        words.append(current);
    return words;
}

QVector<Lyrics::Line> Lyrics::parseTtml(const QString &ttml) {
    QVector<Line> lines;
    QXmlStreamReader xml(ttml);
    xml.setNamespaceProcessing(false); // prefixes such as itunes: are not always declared, and are not needed
    bool inLine = false;
    QVector<QPair<qint64, qint64>> spans; // the spans we are inside of, innermost last (-1 for other elements)
    Line current;
    QString text;
    QVector<QPair<qint64, qint64>> times; // one entry per character of `text`
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("p")) {
            inLine = true;
            spans.clear();
            text.clear();
            times.clear();
            current = Line();
            current.startMs = parseClock(xml.attributes().value(QLatin1String("begin")).toString());
            current.endMs = parseClock(xml.attributes().value(QLatin1String("end")).toString());
        } else if (inLine && xml.isStartElement()) {
            qint64 b = -1, e = -1;
            if (xml.name() == QLatin1String("span")) {
                b = parseClock(xml.attributes().value(QLatin1String("begin")).toString());
                e = parseClock(xml.attributes().value(QLatin1String("end")).toString());
                // Word-timed lines: a line with no begin of its own starts with its first word.
                if (current.startMs < 0)
                    current.startMs = b;
            } else if (xml.name() == QLatin1String("br")) {
                text += QLatin1Char(' ');
                times.append({-1, -1});
            }
            spans.append({b, e});
        } else if (inLine && xml.isEndElement() && !spans.isEmpty()) {
            spans.removeLast();
        } else if (inLine && xml.isCharacters()) {
            // The innermost span that has a time gives these characters theirs.
            QPair<qint64, qint64> t{-1, -1};
            for (int k = spans.size() - 1; k >= 0; --k) {
                if (spans.at(k).first >= 0) {
                    t = spans.at(k);
                    break;
                }
            }
            text += xml.text();
            for (int k = 0; k < xml.text().size(); ++k)
                times.append(t);
        } else if (inLine && xml.isEndElement() && xml.name() == QLatin1String("p")) {
            inLine = false;
            current.text = text.simplified();
            current.words = wordsFrom(text, times);
            if (!current.text.isEmpty())
                lines.append(current);
        }
    }
    if (xml.hasError())
        return {};
    // Timed lines are kept in time order; a file with only some lines timed is treated as untimed.
    const bool allTimed = std::all_of(lines.begin(), lines.end(), [](const Line &l) { return l.startMs >= 0; });
    if (!allTimed) {
        for (auto &line : lines) {
            line.startMs = line.endMs = -1;
            for (auto &word : line.words)
                word.startMs = word.endMs = -1;
        }
    } else {
        std::stable_sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) { return a.startMs < b.startMs; });
    }
    return lines;
}

void Lyrics::ensureWords(QVector<Line> &lines) {
    for (int i = 0; i < lines.size(); ++i) {
        Line &line = lines[i];
        const bool timed = !line.words.isEmpty() && line.words.first().startMs >= 0;
        if (timed)
            continue;
        QVector<Word> words;
        for (const QString &w : line.text.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            Word word;
            word.text = w;
            words.append(word);
        }
        if (line.startMs >= 0 && !words.isEmpty()) {
            // Sung over most of the line, but never slower than about 9 characters a second, so a long gap
            // before the next line does not drag the highlight.
            const qint64 nextStart = i + 1 < lines.size() && lines.at(i + 1).startMs >= 0 ? lines.at(i + 1).startMs : line.startMs + 5000;
            qint64 end = line.endMs > line.startMs ? line.endMs : nextStart;
            end = std::min(end, nextStart);
            const qint64 span = std::max<qint64>(600, std::min<qint64>(end - line.startMs - 120, qint64(line.text.size()) * 110 + 900));
            double total = 0;
            for (const Word &w : words)
                total += w.text.size() + 1;
            double at = 0;
            for (Word &w : words) {
                const double share = (w.text.size() + 1) / total;
                w.startMs = line.startMs + qRound64(at * span);
                at += share;
                w.endMs = line.startMs + qRound64(at * span);
            }
        }
        line.words = words;
    }
}

QVector<Lyrics::Line> Lyrics::parseLrc(const QString &lrc) {
    static const QRegularExpression stamp(QStringLiteral(R"(\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])"));
    static const QRegularExpression offsetTag(QStringLiteral(R"(^\[offset:\s*([+-]?\d+)\s*\]$)"), QRegularExpression::CaseInsensitiveOption);
    QVector<Line> lines;
    qint64 offset = 0;
    for (const QString &raw : lrc.split(QLatin1Char('\n'))) {
        const QString row = raw.trimmed();
        const auto off = offsetTag.match(row);
        if (off.hasMatch()) {
            offset = off.captured(1).toLongLong();
            continue;
        }
        QVector<qint64> times;
        int end = 0;
        auto it = stamp.globalMatch(row);
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedStart() != end)
                break; // stamps must lead the line
            end = m.capturedEnd();
            qint64 ms = (m.captured(1).toLongLong() * 60 + m.captured(2).toLongLong()) * 1000;
            const QString frac = m.captured(3);
            if (!frac.isEmpty()) {
                // ".5" is 500 ms, ".50" is 500 ms, ".500" is 500 ms: the digits are a decimal fraction.
                ms += qRound64(QStringLiteral("0.%1").arg(frac).toDouble() * 1000.0);
            }
            times.append(ms);
        }
        if (times.isEmpty())
            continue; // metadata such as [ar:Artist], or an untimed line
        // Word-by-word LRC ("<00:12.30>") carries the same words; drop the inner stamps.
        QString text = row.mid(end);
        text.remove(QRegularExpression(QStringLiteral(R"(<\d+:\d+(?:[.:]\d+)?>)")));
        text = text.simplified();
        for (qint64 t : times) {
            Line line;
            line.startMs = std::max<qint64>(0, t + offset);
            line.text = text;
            lines.append(line);
        }
    }
    std::stable_sort(lines.begin(), lines.end(), [](const Line &a, const Line &b) { return a.startMs < b.startMs; });
    // Lines with no words are the breathing spaces of the song; they end the previous line but are not shown.
    for (int i = 0; i + 1 < lines.size(); ++i)
        lines[i].endMs = lines[i + 1].startMs;
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const Line &l) { return l.text.isEmpty(); }), lines.end());
    return lines;
}

QVector<Lyrics::Line> Lyrics::parsePlain(const QString &text) {
    QVector<Line> lines;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString row = raw.simplified();
        if (row.isEmpty())
            continue;
        Line line;
        line.text = row;
        lines.append(line);
    }
    return lines;
}

QVector<Lyrics::Line> Lyrics::parseAny(const QString &text) {
    const QString head = text.trimmed().left(512);
    if (head.startsWith(QLatin1Char('<')))
        return parseTtml(text);
    static const QRegularExpression stamp(QStringLiteral(R"(\[\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?\])"));
    if (stamp.match(text).hasMatch()) {
        const auto lines = parseLrc(text);
        if (!lines.isEmpty())
            return lines;
    }
    return parsePlain(text);
}

QVariantList Lyrics::lines() const {
    QVariantList list;
    list.reserve(m_lines.size());
    for (const Line &line : m_lines)
    {
        QVariantList words;
        for (const Word &w : line.words)
            words.append(QVariantMap{{QStringLiteral("time"), w.startMs}, {QStringLiteral("end"), w.endMs}, {QStringLiteral("text"), w.text}});
        list.append(QVariantMap{{QStringLiteral("time"), line.startMs}, {QStringLiteral("end"), line.endMs}, {QStringLiteral("text"), line.text},
                                {QStringLiteral("words"), words}});
    }
    return list;
}

int Lyrics::indexAt(double seconds) const {
    if (!m_synced || m_lines.isEmpty())
        return -1;
    const qint64 ms = qRound64(seconds * 1000.0);
    int low = 0, high = int(m_lines.size()) - 1, found = -1;
    while (low <= high) {
        const int mid = (low + high) / 2;
        if (m_lines.at(mid).startMs <= ms) {
            found = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return found;
}

qint64 Lyrics::startOf(int index) const {
    return index >= 0 && index < m_lines.size() ? m_lines.at(index).startMs : -1;
}

void Lyrics::setLoading() {
    m_lines.clear();
    m_synced = false;
    m_status = QStringLiteral("loading");
    emit changed();
}

void Lyrics::setNone() {
    m_lines.clear();
    m_synced = false;
    m_status = QStringLiteral("none");
    emit changed();
}

void Lyrics::clear() {
    m_lines.clear();
    m_synced = false;
    m_status = QStringLiteral("idle");
    emit changed();
}

bool Lyrics::setText(const QString &text) {
    QVector<Line> lines = parseAny(text);
    ensureWords(lines);
    if (lines.isEmpty()) {
        setNone();
        return false;
    }
    m_lines = lines;
    m_synced = lines.first().startMs >= 0;
    m_status = QStringLiteral("ready");
    emit changed();
    return true;
}
