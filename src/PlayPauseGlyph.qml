import QtQuick
import QtQuick.Shapes

// Play and pause as one shape that morphs between the two.
//
// The play triangle is cut into two four-cornered halves, and the pause icon
// is two bars: each half slides into a bar. `paused` animates on a spring, so
// pressing the button pours one shape into the other instead of swapping icons.
Item {
    id: glyph
    property bool playing: false
    property color color: Glass.ink
    property real size: 24
    width: size
    height: size

    // 0 shows play, 1 shows pause.
    property real t: playing ? 1 : 0
    Behavior on t { SpringAnimation { spring: Glass.springSnap; damping: 0.42; epsilon: 0.002 } }

    function lerp(a, b) { return a + (b - a) * glyph.t }
    // On a 24-unit grid, as the usual Material icons are.
    readonly property real k: size / 24
    function at(px, py, bx, by) { return Qt.point(lerp(px, bx) * k, lerp(py, by) * k) }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        // Left half of the triangle becomes the left bar.
        ShapePath {
            fillColor: glyph.color
            strokeColor: "transparent"
            PathPolyline {
                path: [glyph.at(8, 5, 6, 5), glyph.at(13.5, 8.14, 10, 5), glyph.at(13.5, 15.86, 10, 19),
                       glyph.at(8, 19, 6, 19), glyph.at(8, 5, 6, 5)]
            }
        }
        // Right half (the point) becomes the right bar.
        ShapePath {
            fillColor: glyph.color
            strokeColor: "transparent"
            PathPolyline {
                path: [glyph.at(13.5, 8.14, 14, 5), glyph.at(19, 12, 18, 5), glyph.at(19, 12, 18, 19),
                       glyph.at(13.5, 15.86, 14, 19), glyph.at(13.5, 8.14, 14, 5)]
            }
        }
    }
}
