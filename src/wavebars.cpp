#include "wavebars.h"

#include "audiolevels.h"

#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

double clamp01(double v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

QColor mixColor(const QColor &a, const QColor &b, double t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

// How far the soft edge reaches, in pixels. Half of it is inside the shape and
// half outside, so the shape keeps its size and its edge is smooth.
constexpr double kFeather = 0.8;
constexpr int kCircleSides = 12;
constexpr int kBoxVertices = 16;
constexpr int kBoxIndices = 54;
constexpr int kDotVertices = 1 + 2 * kCircleSides;
constexpr int kDotIndices = (kCircleSides + 2 * kCircleSides) * 3;
// How fast a kick's wave travels along the bars, and how wide it is, in bars.
constexpr double kWaveSpeed = 70.0;
constexpr double kWaveWidth = 3.5;

void setVertex(QSGGeometry::ColoredPoint2D *v, double x, double y, const QColor &c, double alpha) {
    // The scene graph wants premultiplied colour.
    const double a = c.alphaF() * alpha;
    v->set(float(x), float(y), uchar(qRound(c.redF() * a * 255)), uchar(qRound(c.greenF() * a * 255)),
           uchar(qRound(c.blueF() * a * 255)), uchar(qRound(a * 255)));
}

} // namespace

WaveBars::WaveBars(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    m_timer.setInterval(40); // 25 frames a second when there is no sound to follow
    connect(&m_timer, &QTimer::timeout, this, [this] {
        // While the audio is being heard, its own frame clock drives the drawing.
        if (!reactive())
            update();
    });
    m_clock.start();
}

QObject *WaveBars::audio() const {
    return m_audio.data();
}

bool WaveBars::reactive() const {
    return m_audio && m_audio->active() && m_look.energy > 0.01;
}

void WaveBars::setAudio(QObject *object) {
    auto *audio = qobject_cast<AudioLevels *>(object);
    if (audio == m_audio.data())
        return;
    if (m_audio)
        m_audio->disconnect(this);
    m_audio = audio;
    if (m_audio) {
        connect(m_audio, &AudioLevels::levelsChanged, this, [this] {
            if (m_animating && isVisible())
                update();
        });
        connect(m_audio, &AudioLevels::activeChanged, this, [this] { refresh(); });
    }
    emit audioChanged();
    refresh();
}

void WaveBars::refresh() {
    const bool now = reactive();
    if (now != m_wasReactive) {
        m_wasReactive = now;
        emit reactiveChanged();
    }
    update();
}

void WaveBars::restartTimer() {
    if (m_animating && isVisible())
        m_timer.start();
    else
        m_timer.stop();
}

void WaveBars::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange)
        restartTimer();
}

#define SETTER(Name, Type, Field, Signal)  \
    void WaveBars::Name(Type value) {      \
        if (m_look.Field == value)         \
            return;                        \
        m_look.Field = value;              \
        emit Signal();                     \
        update();                          \
    }

SETTER(setStyle, const QString &, style, styleChanged)
SETTER(setBarWidth, double, barWidth, barWidthChanged)
SETTER(setBarGap, double, barGap, barGapChanged)
SETTER(setShown, double, shown, shownChanged)
SETTER(setBeadAt, double, beadAt, beadAtChanged)
SETTER(setFocusAmount, double, focus, focusAmountChanged)
SETTER(setPlayedColor, const QColor &, played, playedColorChanged)
SETTER(setRailColor, const QColor &, rail, railColorChanged)

void WaveBars::setEnergy(double value) {
    if (qFuzzyCompare(m_look.energy + 1, value + 1))
        return;
    m_look.energy = value;
    emit energyChanged();
    refresh();
}

void WaveBars::setPeaks(const QVariantList &peaks) {
    m_peaksList = peaks;
    m_peaks.clear();
    m_peaks.reserve(peaks.size());
    for (const auto &peak : peaks)
        m_peaks.append(peak.toDouble());
    emit peaksChanged();
    update();
}

void WaveBars::setAnimating(bool on) {
    if (on == m_animating)
        return;
    m_animating = on;
    emit animatingChanged();
    restartTimer();
    update();
}

double WaveBars::phaseNow() const {
    // 3.3 radians a second: one ripple every two seconds or so.
    return std::fmod(m_clock.elapsed() / 1000.0 * 3.3, M_PI * 40.0);
}

WaveBars::Look WaveBars::currentLook() const {
    Look look = m_look;
    look.waveHeight = height();
    look.phase = phaseNow();
    return look;
}

WaveBars::Sound WaveBars::currentSound() const {
    Sound sound;
    if (!reactive())
        return sound;
    const auto &frame = m_audio->frame();
    sound.reactive = true;
    sound.level = frame.level;
    sound.bass = frame.bass;
    sound.kick = frame.kick;
    sound.snare = frame.snare;
    sound.hat = frame.hat;
    sound.bands = QVector<double>(frame.bands.begin(), frame.bands.end());
    sound.kicks = m_audio->kickPulses();
    return sound;
}

double WaveBars::bandAt(const QVector<double> &bands, double position) {
    if (bands.size() < 2)
        return 0;
    const double x = clamp01(position) * (bands.size() - 1);
    const int i = int(std::floor(x));
    const double a = bands.at(i);
    const double b = bands.at(std::min(int(bands.size()) - 1, i + 1));
    return a + (b - a) * (x - i);
}

// Each kick sends a ring of motion out from the playhead; this is how strongly the rings reach one bar.
double WaveBars::kickWave(int index, double playheadBar, const QVector<QPair<double, double>> &kicks) {
    double wave = 0;
    for (const auto &kick : kicks) {
        const double age = kick.first;
        const double front = age * kWaveSpeed;
        const double distance = std::abs((index + 0.5) - playheadBar) - front;
        wave += kick.second * std::exp(-age / 0.5) * std::exp(-(distance * distance) / (2 * kWaveWidth * kWaveWidth));
    }
    return std::min(1.4, wave);
}

void WaveBars::stepSpring(Spring &spring, double target, double seconds) {
    // Quick and a little bouncy upwards (a hit), slower and well damped downwards (a decay).
    const bool up = target > spring.x;
    const double stiffness = up ? 520.0 : 190.0;
    const double damping = 2.0 * (up ? 0.65 : 0.9) * std::sqrt(stiffness);
    constexpr int steps = 2;
    const double h = seconds / steps;
    for (int i = 0; i < steps; ++i) {
        spring.v += (stiffness * (target - spring.x) - damping * spring.v) * h;
        spring.x += spring.v * h;
    }
}

// One continuous wave instead of bars: a Catmull-Rom spline through the springs' heights, mirrored about
// the centre, filled with soft (anti-aliased) edges. Five rows of vertices per sample: the outer and inner
// edge on top, the centre line, and the inner and outer edge below. The edge is lighter than the body, and
// on a hi-hat it shimmers along its length.
void WaveBars::buildLiquid(QSGGeometry *geometry, const QVector<Bar> &targets, const Look &look, const Sound &sound,
                           double seconds, bool &moving) {
    const int count = targets.size();
    if (count < 2) {
        geometry->allocate(0, 0);
        return;
    }
    QVector<double> heights(count);
    for (int i = 0; i < count; ++i) {
        stepSpring(m_springs[i], targets.at(i).height, seconds);
        heights[i] = m_springs.at(i).x;
        moving = moving || std::abs(heights.at(i) - targets.at(i).height) > 0.15 || std::abs(m_springs.at(i).v) > 1.0;
    }
    constexpr int sub = 4;               // points between two bars
    constexpr int rows = 5;
    const int samples = (count - 1) * sub + 1;
    geometry->allocate(samples * rows, (samples - 1) * (rows - 1) * 6);
    auto *v = geometry->vertexDataAsColoredPoint2D();
    quint16 *ix = geometry->indexDataAsUShort();

    const double pitch = look.barWidth + look.barGap;
    const double playheadX = look.shown * count * pitch;
    const double centre = look.waveHeight / 2;
    const double f = kFeather / 2;
    const QColor white(255, 255, 255, 255);
    for (int s = 0; s < samples; ++s) {
        const double u = double(s) / sub;
        const int i = std::min(int(u), count - 2);
        const double t = u - i;
        const double p0 = heights.at(std::max(0, i - 1)), p1 = heights.at(i), p2 = heights.at(i + 1),
                     p3 = heights.at(std::min(count - 1, i + 2));
        const double h = std::max(1.6, 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                                               + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t));
        const double x = i * pitch + look.barWidth / 2 + t * pitch;
        const double played = clamp01((playheadX - x) / 2.5 + 0.5);
        const QColor core = mixColor(look.rail, look.played, played);
        // The edge: lighter, a little more opaque, and on a hi-hat a shimmer that runs along it.
        const double spark = sound.hat * (0.5 + 0.5 * std::sin(look.phase * 9.0 + x * 0.37));
        QColor edge = mixColor(core, white, 0.4 + 0.5 * spark);
        edge.setAlphaF(std::min(1.0, core.alphaF() * 1.35 + 0.1 * played));
        const double top = centre - h / 2, bottom = centre + h / 2;
        auto *row = v + s * rows;
        setVertex(row + 0, x, top - f, edge, 0.0);
        setVertex(row + 1, x, top + f, edge, 1.0);
        setVertex(row + 2, x, centre, core, 1.0);
        setVertex(row + 3, x, bottom - f, edge, 1.0);
        setVertex(row + 4, x, bottom + f, edge, 0.0);
    }
    for (int s = 0; s + 1 < samples; ++s)
        for (int r = 0; r + 1 < rows; ++r) {
            const quint16 a = quint16(s * rows + r), b = quint16(a + 1);
            const quint16 c = quint16((s + 1) * rows + r), d = quint16(c + 1);
            *ix++ = a; *ix++ = c; *ix++ = b;
            *ix++ = b; *ix++ = c; *ix++ = d;
        }
}

WaveBars::Bar WaveBars::compute(int index, const QVector<double> &peaks, const Look &look, const Sound &sound) {
    const int count = peaks.size();
    const double peak = index < count ? peaks.at(index) : 0.2;
    const double full = look.waveHeight;
    const QString &style = look.style;
    const bool bounce = style == QLatin1String("bounce");
    const bool minimal = style == QLatin1String("minimal");
    const bool dots = style == QLatin1String("dots");
    const bool liquid = style == QLatin1String("liquid");
    const bool reactive = sound.reactive;

    // How much of this bar has been played, 0 to 1.
    const double played = clamp01(look.shown * count - index);
    const double playhead = look.shown * count;
    // Bars near the bead swell, like liquid drawn up around a finger.
    const double lens = look.focus > 0.01 ? std::exp(-std::pow((index + 0.5) - look.beadAt * count, 2) / 9.0) : 0.0;
    // This bar's slice of the spectrum: bass on the left, treble on the right.
    const double spec = reactive ? bandAt(sound.bands, double(index) / std::max(1, count - 1)) : 0.0;
    const double wave = reactive ? kickWave(index, playhead, sound.kicks) : 0.0;
    // Hats sparkle: each bar flickers at its own rate while one rings.
    const double sparkle = 0.5 + 0.5 * std::sin(look.phase * 7.0 + index * 2.3);
    // Without sound to follow, the played part ripples on its own.
    const double ripple = played > 0 && look.energy > 0.01 && !reactive && !bounce
                              ? 0.6 * std::sin(look.phase * 2 - index * 0.55) + 0.4 * std::sin(look.phase * 3.4 - index * 0.21)
                              : 0.0;
    const double dance = bounce && look.energy > 0.01 && !reactive ? 0.5 + 0.5 * std::sin(look.phase * 3.1 + index * 0.9 + peak * 6) : 0.75;

    double tall;
    if (minimal) {
        tall = reactive ? full * (0.12 + 0.55 * spec + 0.45 * wave) : full * 0.16;
    } else if (bounce) {
        tall = reactive ? full * std::min(1.45, 0.06 + 0.9 * std::pow(spec, 0.9) * (0.65 + 0.35 * peak) + 0.55 * wave + 0.15 * sound.snare)
                        : full * peak * (0.35 + 0.65 * (1 - look.energy + look.energy * dance));
    } else if (reactive) {
        // The song's shape, alive: the spectrum lifts every bar, loudness fills the whole bar, a kick's
        // wave runs out from the playhead and a hat makes the bars flicker.
        tall = full * std::min(1.5, peak * (0.50 + 0.80 * spec + 0.30 * sound.level) + wave * (0.35 + 0.5 * peak)
                                        + 0.22 * sound.hat * sparkle + 0.08 * sound.snare);
        // The liquid wave: a snare makes the whole envelope bounce (the springs make it elastic).
        if (liquid)
            tall *= 1.0 + 0.18 * sound.snare;
    } else {
        tall = full * peak * (1 + ripple * look.energy * 0.16);
    }

    Bar bar;
    bar.dot = dots;
    bar.color = mixColor(look.rail, look.played, played);
    bar.width = dots ? look.barWidth + 1.5 + wave * 3 + sound.snare * 1.5 : look.barWidth;
    // Near the bead the bars swell toward the pointer; the liquid wave is drawn up more, like mercury.
    bar.height = dots ? bar.width : std::max(bar.width, std::min(full * 1.5, tall * (1 + (liquid ? 0.6 : 0.4) * lens * look.focus)));
    bar.x = index * (look.barWidth + look.barGap) + look.barWidth / 2;
    if (dots) {
        // Dots ride the shape of the song's loudness, and the music lifts them.
        const double lift = (ripple * look.energy * 3 + lens * look.focus * 4) * (played > 0 ? 1 : 0);
        bar.centre = full / 2 - (peak - 0.5) * full * 0.9 - spec * full * 0.6 - wave * full * 0.35 - lift;
    } else {
        bar.centre = full / 2;
    }
    return bar;
}

double WaveBars::barHeight(int index) const {
    if (index < 0 || index >= m_peaks.size())
        return 0;
    // Once drawn, the spring's position; before that, where it is headed.
    if (index < m_springs.size() && m_springStyle == m_look.style)
        return m_look.style == QLatin1String("dots") ? compute(index, m_peaks, currentLook(), currentSound()).height : m_springs.at(index).x;
    return compute(index, m_peaks, currentLook(), currentSound()).height;
}

double WaveBars::barCentre(int index) const {
    if (index < 0 || index >= m_peaks.size())
        return 0;
    if (index < m_springs.size() && m_springStyle == m_look.style && m_look.style == QLatin1String("dots"))
        return m_springs.at(index).x;
    return compute(index, m_peaks, currentLook(), currentSound()).centre;
}

QSGNode *WaveBars::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    auto *node = static_cast<QSGGeometryNode *>(old);
    if (!node) {
        node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        auto *material = new QSGVertexColorMaterial;
        material->setFlag(QSGMaterial::Blending);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
    }

    const int count = m_peaks.size();
    const bool dots = m_look.style == QLatin1String("dots");
    const bool bounce = m_look.style == QLatin1String("bounce");
    const Look look = currentLook();
    const Sound sound = currentSound();

    // Real time since the last drawn frame, so the springs behave the same at any refresh rate.
    const qint64 nowNs = m_clock.nsecsElapsed();
    const double seconds = m_lastDrawNs > 0 ? qBound(0.002, double(nowNs - m_lastDrawNs) / 1e9, 0.05) : 1.0 / 60;
    m_lastDrawNs = nowNs;

    QVector<Bar> targets(count);
    for (int i = 0; i < count; ++i)
        targets[i] = compute(i, m_peaks, look, sound);
    bool moving = false; // set below if any spring has not come to rest
    // A new song, width or style starts the springs at rest, where they belong.
    if (m_springs.size() != count || m_springStyle != m_look.style) {
        m_springs.resize(count);
        m_caps = QVector<double>(count, 0.0);
        m_capSpeed = QVector<double>(count, 0.0);
        m_capHold = QVector<double>(count, 0.0);
        for (int i = 0; i < count; ++i) {
            m_springs[i].x = dots ? targets.at(i).centre : targets.at(i).height;
            m_springs[i].v = 0;
        }
        m_springStyle = m_look.style;
    }

    if (m_look.style == QLatin1String("liquid")) {
        buildLiquid(node->geometry(), targets, look, sound, seconds, moving);
        node->markDirty(QSGNode::DirtyGeometry);
        if (moving && !(m_animating && reactive()))
            QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        return node;
    }

    // What is drawn: boxes and dots, each with its own soft edge.
    struct Shape {
        double x, centre, width, height;
        bool dot;
        QColor color;
    };
    QVector<Shape> shapes;
    shapes.reserve(count * (bounce ? 3 : 1));
    for (int i = 0; i < count; ++i) {
        const Bar &target = targets.at(i);
        const double goal = dots ? target.centre : target.height;
        stepSpring(m_springs[i], goal, seconds);
        const double value = m_springs.at(i).x;
        moving = moving || std::abs(value - goal) > 0.15 || std::abs(m_springs.at(i).v) > 1.0;
        if (dots) {
            shapes.append({target.x, value, target.width, target.width, true, target.color});
            continue;
        }
        const double height = std::max(target.width, value);
        shapes.append({target.x, target.centre, target.width, height, false, target.color});
        if (bounce) {
            // Peak caps: hold a moment above the bar's best, then fall faster and faster.
            if (height >= m_caps.at(i)) {
                m_caps[i] = height;
                m_capSpeed[i] = 0;
                m_capHold[i] = 0.14;
            } else if (m_capHold.at(i) > 0) {
                m_capHold[i] -= seconds;
            } else {
                m_capSpeed[i] += 780.0 * seconds;
                m_caps[i] = std::max(height, m_caps.at(i) - m_capSpeed.at(i) * seconds);
            }
            const QColor cap = mixColor(target.color, QColor(255, 255, 255, 255), 0.55);
            const double offset = m_caps.at(i) / 2 + 3.0;
            shapes.append({target.x, target.centre - offset, target.width, 2.2, false, cap});
            shapes.append({target.x, target.centre + offset, target.width, 2.2, false, cap});
        }
    }

    int boxes = 0, circles = 0;
    for (const Shape &s : shapes)
        (s.dot ? circles : boxes)++;
    QSGGeometry *geometry = node->geometry();
    geometry->allocate(boxes * kBoxVertices + circles * kDotVertices, boxes * kBoxIndices + circles * kDotIndices);
    auto *vertices = geometry->vertexDataAsColoredPoint2D();
    quint16 *indices = geometry->indexDataAsUShort();

    const double f = kFeather / 2;
    int vertex = 0;
    for (const Shape &shape : shapes) {
        auto *v = vertices + vertex;
        const quint16 base = quint16(vertex);
        if (shape.dot) {
            const double r = shape.width / 2;
            setVertex(v, shape.x, shape.centre, shape.color, 1.0);
            for (int s = 0; s < kCircleSides; ++s) {
                const double angle = 2 * M_PI * s / kCircleSides;
                setVertex(v + 1 + s, shape.x + (r - f) * std::cos(angle), shape.centre + (r - f) * std::sin(angle), shape.color, 1.0);
                setVertex(v + 1 + kCircleSides + s, shape.x + (r + f) * std::cos(angle), shape.centre + (r + f) * std::sin(angle), shape.color, 0.0);
            }
            for (int s = 0; s < kCircleSides; ++s) {
                const quint16 next = quint16((s + 1) % kCircleSides);
                const quint16 in0 = base + 1 + s, in1 = base + 1 + next;
                const quint16 out0 = base + 1 + kCircleSides + s, out1 = base + 1 + kCircleSides + next;
                *indices++ = base; *indices++ = in0; *indices++ = in1;
                *indices++ = in0; *indices++ = out0; *indices++ = out1;
                *indices++ = in0; *indices++ = out1; *indices++ = in1;
            }
            vertex += kDotVertices;
        } else {
            const double left = shape.x - shape.width / 2, right = shape.x + shape.width / 2;
            const double top = shape.centre - shape.height / 2, bottom = shape.centre + shape.height / 2;
            const double xs[4] = {left - f, left + f, right - f, right + f};
            const double ys[4] = {top - f, top + f, bottom - f, bottom + f};
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col) {
                    const bool inside = row >= 1 && row <= 2 && col >= 1 && col <= 2;
                    setVertex(v + row * 4 + col, xs[col], ys[row], shape.color, inside ? 1.0 : 0.0);
                }
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col) {
                    const quint16 a = base + quint16(row * 4 + col), b = quint16(a + 1);
                    const quint16 c = quint16(a + 4), d = quint16(a + 5);
                    *indices++ = a; *indices++ = b; *indices++ = c;
                    *indices++ = b; *indices++ = d; *indices++ = c;
                }
            vertex += kBoxVertices;
        }
    }
    node->markDirty(QSGNode::DirtyGeometry);
    // When the sound stops (a pause), the bars still have a moment's motion left in them. Keep drawing
    // until they are at rest, instead of freezing half way. (Queued: this runs on the render thread.)
    if (moving && !(m_animating && reactive()))
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    return node;
}
