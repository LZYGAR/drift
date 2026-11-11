#include "commands.h"

#include <QVariantMap>
#include <QRegularExpression>
#include <array>
#include <cmath>
#include <vector>
#include <algorithm>

int Commands::score(const QString &query, const QString &text) {
    const QString needle = query.simplified().toLower();
    const QString hay = text.toLower();
    if (needle.isEmpty())
        return 0;
    int total = 0;
    int from = 0;
    int previous = -2;
    for (const QChar ch : needle) {
        if (ch.isSpace())
            continue;
        const int found = hay.indexOf(ch, from);
        if (found < 0)
            return -1;
        total += 10;
        if (found == previous + 1)
            total += 8; // runs of letters beat scattered ones
        if (found == 0 || !hay.at(found - 1).isLetterOrNumber())
            total += 6; // start of a word
        total -= qMin(5, found - from); // skipped letters cost a little
        previous = found;
        from = found + 1;
    }
    if (hay.startsWith(needle))
        total += 20;
    else if (hay.contains(needle))
        total += 10;
    return qMax(1, total);
}

QVariantList Commands::rank(const QString &query, const QVariantList &items, int limit) const {
    struct Hit {
        int score;
        int order;
        QVariant item;
    };
    QVector<Hit> hits;
    for (int i = 0; i < items.size(); ++i) {
        const QVariantMap item = items.at(i).toMap();
        const int label = score(query, item.value(QStringLiteral("label")).toString());
        const int keywords = score(query, item.value(QStringLiteral("keywords")).toString());
        // A hit on the visible label outranks one on the hidden keywords.
        const int best = qMax(label, keywords >= 0 ? keywords / 2 : -1);
        if (best < 0)
            continue;
        hits.append({best, i, items.at(i)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) { return a.score > b.score; });
    QVariantList out;
    for (const auto &hit : hits) {
        if (limit > 0 && out.size() >= limit)
            break;
        out.append(hit.item);
    }
    return out;
}

namespace {

struct Flavor {
    const char *words; // alternatives separated by '|', matched as whole words or phrases
    const char *name;
    std::array<double, 10> bands;
    double reverb;
};

// Each word is a nudge on the ten bands (31 Hz ... 16 kHz) and on the room; the nudges are summed.
const Flavor kFlavors[] = {
    {"bass boost|bass|boomy|thump|thumpy|subwoofer|subs|low end|heavy|rumble", "bass", {6, 6, 4, 2, 0, 0, 0, 0, 0, 0}, 0},
    {"treble|highs|high end|shimmer", "treble", {0, 0, 0, 0, 0, 1, 3, 5, 6, 6}, 0},
    {"mids|midrange", "mids", {0, 0, 0, 1, 3, 4, 4, 2, 0, 0}, 0},
    {"muffled|muffle|muddy|underwater|dull|behind a wall|through a wall|next door|neighbour|neighbor", "muffled", {2, 2, 1, 0, -1, -3, -5, -7, -9, -11}, 0},
    {"club|party|dance|edm|techno|house|punch|punchy", "club", {6, 5, 3, 0, -1, -1, 0, 2, 3, 3}, 0.05},
    {"bathroom|shower|tiled|tiles|stairwell", "bathroom", {0, 0, 0, 0, 0, 0, 2, 3, 2, 0}, 0.45},
    {"concert|hall|cathedral|church|arena|stadium", "hall", {1, 1, 0, 0, 0, 0, 0, 0, 0, 0}, 0.65},
    {"reverb|room|echo|spacious|roomy|ambient|dreamy", "room", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0.4},
    {"crisp|crispy|clear|clarity|bright|airy|sparkle|sparkly|acoustic|acoustics|detail|detailed", "crisp", {-2, -1, 0, 0, 1, 2, 3, 4, 5, 5}, 0},
    {"vocal|vocals|voice|podcast|speech|dialogue|singer", "vocals", {-3, -2, 0, 1, 3, 5, 5, 3, 1, 0}, 0},
    {"intimate|late night|night|cozy|cosy|soft|mellow|smooth|sleepy|chill", "late night", {2, 2, 1, 0, -1, -2, -3, -4, -5, -6}, 0.15},
    {"warm|warmth|vintage|vinyl|tube|analog|analogue", "warm", {3, 3, 2, 1, 0, -1, -2, -2, -3, -3}, 0},
    {"harsh|sibilant|piercing|shrill|tinny|fatiguing", "less harsh", {0, 0, 0, 0, 0, 0, -1, -3, -5, -4}, 0},
    {"loud|energetic|energy|hype|workout|gym", "energy", {4, 3, 1, 0, 0, 1, 2, 3, 3, 2}, 0},
    {"flat|neutral|reset|studio|natural|original|off", "flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0},
};

bool hasWord(const QStringList &tokens, const QString &phrase, int &at) {
    const QStringList want = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i + want.size() <= tokens.size(); ++i) {
        bool same = true;
        for (int j = 0; j < want.size() && same; ++j)
            same = tokens.at(i + j) == want.at(j);
        if (same) {
            at = i;
            return true;
        }
    }
    return false;
}

} // namespace

QVariantMap Commands::describeSound(const QString &text) {
    const QStringList tokens = text.toLower().split(QRegularExpression(QStringLiteral("[^a-z0-9]+")), Qt::SkipEmptyParts);
    static const QStringList negators{QStringLiteral("less"), QStringLiteral("no"), QStringLiteral("cut"), QStringLiteral("lower"),
                                      QStringLiteral("without"), QStringLiteral("minus"), QStringLiteral("reduce"), QStringLiteral("not")};
    static const QStringList softeners{QStringLiteral("slight"), QStringLiteral("slightly"), QStringLiteral("touch"), QStringLiteral("little"),
                                       QStringLiteral("bit"), QStringLiteral("subtle"), QStringLiteral("gentle")};
    static const QStringList boosters{QStringLiteral("very"), QStringLiteral("super"), QStringLiteral("extra"), QStringLiteral("ultra"),
                                      QStringLiteral("more"), QStringLiteral("massive"), QStringLiteral("max"), QStringLiteral("much")};

    // Words that carry no sound of their own ("make it sound like a club").
    static const QStringList filler{QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("or"),
                                    QStringLiteral("with"), QStringLiteral("in"), QStringLiteral("of"), QStringLiteral("my"), QStringLiteral("it"),
                                    QStringLiteral("me"), QStringLiteral("sound"), QStringLiteral("sounds"), QStringLiteral("like"), QStringLiteral("make"),
                                    QStringLiteral("music"), QStringLiteral("please"), QStringLiteral("feel"), QStringLiteral("feels"), QStringLiteral("some"),
                                    QStringLiteral("more"), QStringLiteral("less")};
    QVector<bool> known(tokens.size(), false);
    for (int i = 0; i < tokens.size(); ++i)
        known[i] = negators.contains(tokens.at(i)) || softeners.contains(tokens.at(i)) || boosters.contains(tokens.at(i)) || filler.contains(tokens.at(i));
    std::array<double, 10> sum{};
    double reverb = 0;
    QStringList understood;
    bool flat = false;
    for (const auto &flavor : kFlavors) {
        int at = -1;
        bool found = false;
        for (const QString &phrase : QString::fromLatin1(flavor.words).split(QLatin1Char('|'))) {
            // "off" and "room" are common words: they only count as a whole word, which hasWord already ensures.
            if (hasWord(tokens, phrase, at)) {
                found = true;
                break;
            }
        }
        if (!found)
            continue;
        // Every word of the matched phrase counts as understood.
        for (const QString &phrase : QString::fromLatin1(flavor.words).split(QLatin1Char('|'))) {
            int where = -1;
            if (hasWord(tokens, phrase, where)) {
                for (int k = 0; k < phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts).size(); ++k)
                    known[where + k] = true;
            }
        }
        double weight = 1.0;
        // The word just before (or two before, past one modifier) decides the sign and size.
        for (int back = 1; back <= 2 && at - back >= 0; ++back) {
            const QString before = tokens.at(at - back);
            if (negators.contains(before))
                weight = -weight;
            else if (softeners.contains(before))
                weight *= 0.5;
            else if (boosters.contains(before))
                weight *= 1.5;
        }
        if (QLatin1String(flavor.name) == QLatin1String("flat"))
            flat = true;
        for (int i = 0; i < 10; ++i)
            sum[i] += flavor.bands[i] * weight;
        reverb += flavor.reverb * weight;
        understood << QString::fromLatin1(flavor.name);
    }
    if (understood.isEmpty())
        return {};

    QVariantList bands;
    double low = 0, middle = 0, high = 0;
    for (int i = 0; i < 10; ++i) {
        const double db = flat ? 0.0 : std::max(-12.0, std::min(12.0, sum[i]));
        bands.append(db);
        if (i <= 2) low += db / 3;
        else if (i >= 4 && i <= 6) middle += db / 3;
        else if (i >= 7) high += db / 3;
    }
    QVariantMap out;
    out.insert(QStringLiteral("summary"), understood.join(QStringLiteral(", ")));
    // True when every word was understood: "muffled club bathroom" is a sound; "night drive" is a song.
    out.insert(QStringLiteral("complete"), !known.contains(false));
    out.insert(QStringLiteral("bands"), bands);
    out.insert(QStringLiteral("reverb"), flat ? 0.0 : std::max(0.0, std::min(1.0, reverb)));
    out.insert(QStringLiteral("bass"), std::max(-1.0, std::min(1.0, low / 10.0)));
    out.insert(QStringLiteral("mid"), std::max(-1.0, std::min(1.0, middle / 8.0)));
    out.insert(QStringLiteral("treble"), std::max(-1.0, std::min(1.0, high / 10.0)));
    return out;
}
