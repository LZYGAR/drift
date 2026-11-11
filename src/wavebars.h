#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <QSGGeometry>

#include "audiolevels.h"

// The bars of the time bar, drawn as one scene-graph node.
//
// Every bar is worked out here in C++ from the song's shape, how much has been
// played, the pointer, and (when the player can hear itself) the audio, so a
// frame costs one small function call and one buffer upload however often the
// music moves it.
//
// What makes it move like music rather than like an animation:
//  * Every bar has a spring. The music sets where it should be; the spring
//    gets it there with a little overshoot, fast on the way up and slower on
//    the way down, using the real time between frames.
//  * The whole bar shimmers with the spectrum (bass on the left, treble on the
//    right) in every style, not only near the playhead.
//  * Each kick sends a wave out from the playhead along the bars, fading as it
//    goes; hats add sparkle and snares a flick.
//  * The equalizer style keeps a peak cap above each bar that holds for a
//    moment, then falls under gravity.
//
// Styles: "waveform" (the song's shape, alive with the music), "bounce" (a
// spectrum analyser), "dots" and "minimal".
class WaveBars : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariantList peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY styleChanged)
    Q_PROPERTY(double barWidth READ barWidth WRITE setBarWidth NOTIFY barWidthChanged)
    Q_PROPERTY(double barGap READ barGap WRITE setBarGap NOTIFY barGapChanged)
    Q_PROPERTY(double shown READ shown WRITE setShown NOTIFY shownChanged)
    Q_PROPERTY(double beadAt READ beadAt WRITE setBeadAt NOTIFY beadAtChanged)
    Q_PROPERTY(double focusAmount READ focusAmount WRITE setFocusAmount NOTIFY focusAmountChanged)
    Q_PROPERTY(double energy READ energy WRITE setEnergy NOTIFY energyChanged)
    Q_PROPERTY(QColor playedColor READ playedColor WRITE setPlayedColor NOTIFY playedColorChanged)
    Q_PROPERTY(QColor railColor READ railColor WRITE setRailColor NOTIFY railColorChanged)
    Q_PROPERTY(bool animating READ animating WRITE setAnimating NOTIFY animatingChanged)
    Q_PROPERTY(QObject *audio READ audio WRITE setAudio NOTIFY audioChanged)
    Q_PROPERTY(bool reactive READ reactive NOTIFY reactiveChanged)

public:
    explicit WaveBars(QQuickItem *parent = nullptr);

    // What one bar looks like: a box, or a circle when `dot`. `x` and `centre`
    // are the middle of it, so both shapes use the same fields.
    struct Bar {
        double x = 0;
        double centre = 0;
        double width = 0;
        double height = 0;
        bool dot = false;
        QColor color;
    };

    // The audio the bars react to, all 0 to 1. Passed in so the arithmetic can be tested.
    struct Sound {
        bool reactive = false;
        double level = 0;
        double bass = 0;
        double kick = 0;   // fades after each kick
        double snare = 0;
        double hat = 0;
        QVector<double> bands;
        QVector<QPair<double, double>> kicks; // (seconds ago, strength) of recent kicks
    };

    struct Look {
        QString style = QStringLiteral("waveform");
        double barWidth = 2.5;
        double barGap = 1.5;
        double waveHeight = 46;
        double shown = 0;
        double beadAt = 0;
        double focus = 0;
        double energy = 0;
        double phase = 0;
        QColor played;
        QColor rail;
    };

    // Where bar `index` wants to be right now (its target; the spring follows it).
    static Bar compute(int index, const QVector<double> &peaks, const Look &look, const Sound &sound);
    static double bandAt(const QVector<double> &bands, double position);
    // How strongly the waves of recent kicks reach bar `index`: 0 far from any, about 1 on a fresh one.
    static double kickWave(int index, double playheadBar, const QVector<QPair<double, double>> &kicks);

    // One step of a bar's spring towards `target`. Quick and a little bouncy upwards, slower downwards.
    struct Spring {
        double x = 0;
        double v = 0;
    };
    static void stepSpring(Spring &spring, double target, double seconds);

    QVariantList peaks() const { return m_peaksList; }
    QString style() const { return m_look.style; }
    double barWidth() const { return m_look.barWidth; }
    double barGap() const { return m_look.barGap; }
    double shown() const { return m_look.shown; }
    double beadAt() const { return m_look.beadAt; }
    double focusAmount() const { return m_look.focus; }
    double energy() const { return m_look.energy; }
    QColor playedColor() const { return m_look.played; }
    QColor railColor() const { return m_look.rail; }
    bool animating() const { return m_animating; }
    QObject *audio() const;
    bool reactive() const;

    void setPeaks(const QVariantList &peaks);
    void setStyle(const QString &style);
    void setBarWidth(double value);
    void setBarGap(double value);
    void setShown(double value);
    void setBeadAt(double value);
    void setFocusAmount(double value);
    void setEnergy(double value);
    void setPlayedColor(const QColor &color);
    void setRailColor(const QColor &color);
    void setAnimating(bool on);
    void setAudio(QObject *audio);

    // For tests and screenshots: where a bar is right now (its spring, once drawn).
    Q_INVOKABLE double barHeight(int index) const;
    Q_INVOKABLE double barCentre(int index) const;

signals:
    void peaksChanged();
    void styleChanged();
    void barWidthChanged();
    void barGapChanged();
    void shownChanged();
    void beadAtChanged();
    void focusAmountChanged();
    void energyChanged();
    void playedColorChanged();
    void railColorChanged();
    void animatingChanged();
    void audioChanged();
    void reactiveChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    Look currentLook() const;
    Sound currentSound() const;
    double phaseNow() const;
    void refresh();
    void restartTimer();
    void buildLiquid(QSGGeometry *geometry, const QVector<Bar> &targets, const Look &look, const Sound &sound, double seconds,
                     bool &moving);

    QVector<double> m_peaks;
    QVariantList m_peaksList;
    Look m_look;
    bool m_animating = false;
    bool m_wasReactive = false;
    QPointer<AudioLevels> m_audio;
    QTimer m_timer;
    QElapsedTimer m_clock;
    // The springs. Only touched while drawing.
    QVector<Spring> m_springs;
    QVector<double> m_caps, m_capSpeed, m_capHold;
    QString m_springStyle;
    qint64 m_lastDrawNs = 0;
};
