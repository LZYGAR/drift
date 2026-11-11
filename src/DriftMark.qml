import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// The word "drift", drawn as five bold round-ended strokes instead of a font, so it looks the same on every machine.
//
// `progress` 0 to 1 draws the letters on, one after another, each stroke running along its own path; the dot of the
// i then drops in with a bounce. The ink is liquid colour (the same flowing field as the full-screen view) seen
// through the letters, with a soft glow behind. Once drawn, each letter sways a little on its own beat, so the word
// drifts. Nothing is a font: the shapes are in `glyphs` below, in a 300 x 130 design grid.
Item {
    id: mark

    property real markWidth: 300
    property real progress: 1
    property bool running: true
    property real glow: 1
    // Brand colours: not the cover's, so the name looks the same whatever is playing.
    property color inkA: "#ff6fae"
    property color inkB: "#6f8dff"
    property color inkC: "#2b1750"

    readonly property real u: markWidth / 300
    width: markWidth
    height: 130 * u

    // Letters: x is where the letter starts, strokes are SVG paths in design units, len is about how long each is.
    readonly property var glyphs: [
        { x: 11, strokes: [
            { d: "M 0 72 C 0 56.53 12.53 44 28 44 C 43.47 44 56 56.53 56 72 C 56 87.47 43.47 100 28 100 C 12.53 100 0 87.47 0 72", len: 190 },
            { d: "M 56 6 L 56 100", len: 104 } ] },
        { x: 93, strokes: [
            { d: "M 0 46 L 0 100", len: 60 },
            { d: "M 0 76 C 0 56 12 47 34 48", len: 48 } ] },
        { x: 153, strokes: [
            { d: "M 0 46 L 0 100", len: 60 } ], dot: true },
        { x: 193, strokes: [
            { d: "M 0 100 L 0 32 C 0 12 14 6 36 10", len: 124 },
            { d: "M -14 48 L 24 48", len: 42 } ] },
        { x: 259, strokes: [
            { d: "M 0 16 L 0 84 C 0 98 12 103 30 99", len: 118 },
            { d: "M -14 48 L 26 48", len: 44 } ] }
    ]

    // Time for the sway, advanced only while shown.
    property real beat: 0
    FrameAnimation {
        running: mark.running && mark.visible && mark.progress >= 1
        onTriggered: mark.beat += frameTime
    }

    function easeOutBack(t) {
        const c = 1.9
        const x = Math.max(0, Math.min(1, t)) - 1
        return 1 + (c + 1) * x * x * x + c * x * x
    }
    // How far letter `i` is drawn, 0 to 1: they start 0.14 apart and each takes 0.44 of the whole.
    function letterProgress(i) {
        return Math.max(0, Math.min(1, (progress - i * 0.14) / 0.44))
    }

    // Colour, shown only through the letters: a slow tide of pink, violet and cyan, with a bright edge crossing it.
    Item {
        id: ink
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle {
            id: tide
            width: mark.width * 2
            height: parent.height
            x: -((mark.beat * 38 + mark.progress * 90) % mark.width)
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: mark.inkA }
                GradientStop { position: 0.25; color: "#a56bff" }
                GradientStop { position: 0.5; color: mark.inkB }
                GradientStop { position: 0.75; color: "#a56bff" }
                GradientStop { position: 1.0; color: mark.inkA }
            }
        }
        // lighter on top, deeper underneath
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#40ffffff" }
                GradientStop { position: 0.6; color: "transparent" }
                GradientStop { position: 1; color: "#33000000" }
            }
        }
        Rectangle {
            width: parent.width * 0.3
            height: parent.height * 1.4
            y: -parent.height * 0.2
            rotation: 18
            x: (mark.beat * 90 % (parent.width * 2)) - parent.width * 0.4
            opacity: 0.35 * Math.min(1, mark.progress)
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.5; color: "#ffffffff" }
                GradientStop { position: 1; color: "transparent" }
            }
        }
    }

    // The letters themselves (the mask).
    Item {
        id: letters
        anchors.fill: parent
        visible: false
        layer.enabled: true
        layer.smooth: true
        layer.samples: 4

        Repeater {
            model: mark.glyphs
            Item {
                id: letter
                required property var modelData
                required property int index
                readonly property real p: mark.letterProgress(index)
                x: modelData.x * mark.u
                y: (10 + 1.6 * Math.sin(mark.beat * 1.3 + index * 0.9)) * mark.u
                rotation: 1.6 * Math.sin(mark.beat * 0.9 + index * 1.7)
                transformOrigin: Item.Bottom

                Repeater {
                    model: letter.modelData.strokes
                    Shape {
                        id: seg
                        required property var modelData
                        readonly property real sw: 18 * mark.u
                        // One long dash as long as the stroke; sliding it draws the stroke on.
                        readonly property real dash: modelData.len * 1.08 * mark.u / sw
                        opacity: letter.p > 0 ? 1 : 0
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: "white"
                            strokeWidth: seg.sw
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            strokeStyle: ShapePath.DashLine
                            dashPattern: [seg.dash, 400]
                            dashOffset: seg.dash * (1 - letter.p)
                            PathSvg { path: mark.scaled(seg.modelData.d) }
                        }
                    }
                }
                // the dot of the i drops in once the stem is there
                Rectangle {
                    visible: letter.modelData.dot === true
                    readonly property real s: mark.easeOutBack((letter.p - 0.72) / 0.28)
                    width: 25 * mark.u * s
                    height: width
                    radius: width / 2
                    x: -width / 2
                    y: (14 - 40 * (1 - s)) * mark.u - height / 2
                    opacity: letter.p > 0.72 ? 1 : 0
                    color: "white"
                }
            }
        }
    }

    // `d` with every number scaled by u.
    function scaled(d) {
        return d.replace(/-?\d+(\.\d+)?/g, function (n) { return (parseFloat(n) * mark.u).toFixed(2) })
    }

    // the ink, through the letters
    MultiEffect {
        anchors.fill: parent
        source: ink
        maskEnabled: true
        maskSource: letters
    }
}
