#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

// The shape drawn in the time bar.
//
// Apple's audio is DRM-protected, so its real PCM peaks cannot be read. This
// builds a believable stand-in instead: a song-like energy arc (intro, verses,
// louder choruses, a breakdown, an outro) with harmonic detail on top. It is
// seeded from the song, so the same song always gets the same shape.
class Waveform : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // `count` peaks in 0.08..1. Empty when count is not positive.
    static QVector<double> generate(const QString &key, double durationSeconds, int count);
    // The quiet dip nearest to `ratio` (a phrase boundary), if one lies within
    // `reach` (a fraction of the song). Otherwise `ratio` unchanged.
    static double snapRatio(const QVector<double> &peaks, double ratio, double reach);

    Q_INVOKABLE QVariantList peaks(const QString &key, double durationSeconds, int count) const;
    Q_INVOKABLE double snap(const QVariantList &peaks, double ratio, double reach) const;
};
