#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

// Fuzzy matching for the command palette (Ctrl+K).
class Commands : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // How well `query` matches `text`: its letters must appear in order. Higher
    // is better; -1 means no match. An empty query matches everything with 0.
    static int score(const QString &query, const QString &text);

    // Keeps the items that match, best first. Each item is a map with a
    // "label" and optional "keywords"; ties keep their original order.
    // Turns a described sound ("muffled club bathroom", "bass boost", "crisp acoustics") into EQ settings.
    // Returns an empty map when no word is recognised; otherwise:
    //   summary  the words that were understood, e.g. "muffled, club, bathroom"
    //   complete true when every word was understood (so it is surely a sound, not a song title)
    //   bands    ten gains in dB (31 Hz to 16 kHz), clamped to +-12
    //   reverb   0..1
    //   bass, mid, treble   the same shape for Simple mode (-1..1)
    // Words add up; "less/no/cut" before a word flips it, "slight/touch" halves it, "very/extra" boosts it.
    Q_INVOKABLE static QVariantMap describeSound(const QString &text);

    Q_INVOKABLE QVariantList rank(const QString &query, const QVariantList &items, int limit = 7) const;
};
