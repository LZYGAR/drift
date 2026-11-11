#include "waveform.h"

#include <QtMath>

namespace {

// Small deterministic generator, so a song's shape never changes between runs.
struct Rng {
    quint32 state;
    double next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return double(state) / 4294967295.0;
    }
    double range(double low, double high) { return low + (high - low) * next(); }
};

quint32 seedOf(const QString &key, double duration) {
    const QByteArray bytes = key.toUtf8();
    quint32 hash = 2166136261u;
    for (const char byte : bytes) {
        hash ^= quint32(quint8(byte));
        hash *= 16777619u;
    }
    hash ^= quint32(qMax(0.0, duration) * 10.0);
    return hash ? hash : 0x9E3779B9u;
}

} // namespace

QVector<double> Waveform::generate(const QString &key, double durationSeconds, int count) {
    QVector<double> out;
    if (count <= 0)
        return out;
    out.resize(count);
    Rng rng{seedOf(key, durationSeconds)};
    for (int i = 0; i < 4; ++i)
        rng.next();

    // Section edges as fractions of the song, with a level for each section.
    QVector<double> edges{0.0};
    QVector<double> levels;
    const double introEnd = rng.range(0.04, 0.08);
    const double outroStart = rng.range(0.90, 0.95);
    edges.append(introEnd);
    levels.append(rng.range(0.28, 0.42));
    bool chorus = false;
    bool brokeDown = false;
    double at = introEnd;
    while (at < outroStart - 0.06) {
        at = qMin(outroStart, at + rng.range(0.09, 0.16));
        double level = chorus ? rng.range(0.82, 1.0) : rng.range(0.48, 0.66);
        // One quieter passage past the middle, like a bridge.
        if (!brokeDown && !chorus && at > 0.58) {
            level = rng.range(0.26, 0.36);
            brokeDown = true;
        }
        edges.append(at);
        levels.append(level);
        chorus = !chorus;
    }
    if (edges.last() < outroStart) {
        edges.append(outroStart);
        levels.append(rng.range(0.7, 0.9));
    }
    edges.append(1.0);
    levels.append(rng.range(0.3, 0.45));

    // Harmonic detail: a few sines at related rates, each with its own phase.
    const double base = rng.range(5.0, 9.0);
    const double phase1 = rng.range(0, 2 * M_PI);
    const double phase2 = rng.range(0, 2 * M_PI);
    const double phase3 = rng.range(0, 2 * M_PI);

    for (int i = 0; i < count; ++i) {
        const double t = (i + 0.5) / count;
        int section = 0;
        while (section < levels.size() - 1 && t >= edges.at(section + 1))
            ++section;
        const double start = edges.at(section);
        const double end = edges.at(section + 1);
        double level = levels.at(section);
        // Dip towards each section edge, so the boundaries read as quiet moments.
        const double edge = qMin(t - start, end - t);
        const double dip = qBound(0.0, edge / 0.012, 1.0);
        level *= 0.42 + 0.58 * dip;
        if (section == 0)
            level *= 0.35 + 0.65 * (t / qMax(0.001, end));
        if (section == levels.size() - 1)
            level *= 0.2 + 0.8 * ((1.0 - t) / qMax(0.001, 1.0 - start));

        const double x = t * 2 * M_PI;
        const double detail = 0.5 * qSin(x * base * 6 + phase1) + 0.3 * qSin(x * base * 13 + phase2)
                              + 0.2 * qSin(x * base * 29 + phase3);
        const double grain = rng.range(-1.0, 1.0);
        const double value = level * (0.78 + 0.17 * detail + 0.09 * grain);
        out[i] = qBound(0.08, value, 1.0);
    }
    return out;
}

double Waveform::snapRatio(const QVector<double> &peaks, double ratio, double reach) {
    const int count = peaks.size();
    ratio = qBound(0.0, ratio, 1.0);
    if (count < 5 || !(reach > 0))
        return ratio;
    double mean = 0;
    for (const double peak : peaks)
        mean += peak;
    mean /= count;

    double best = ratio;
    double bestDistance = reach;
    for (int i = 1; i < count - 1; ++i) {
        const double value = peaks.at(i);
        if (value > peaks.at(i - 1) || value > peaks.at(i + 1) || value > mean * 0.72)
            continue;
        const double centre = (i + 0.5) / count;
        const double distance = qAbs(centre - ratio);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = centre;
        }
    }
    return best;
}

QVariantList Waveform::peaks(const QString &key, double durationSeconds, int count) const {
    QVariantList list;
    const auto values = generate(key, durationSeconds, qBound(0, count, 512));
    list.reserve(values.size());
    for (const double value : values)
        list.append(value);
    return list;
}

double Waveform::snap(const QVariantList &peaks, double ratio, double reach) const {
    QVector<double> values;
    values.reserve(peaks.size());
    for (const auto &peak : peaks)
        values.append(peak.toDouble());
    return snapRatio(values, ratio, reach);
}
