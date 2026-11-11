import QtQuick
import QtQuick.Shapes

// One path-drawn glyph on a 24px grid, so no icon font is needed.
Shape {
    id: glyph
    property string path: ""
    property color color: Glass.ink
    property real size: 24
    width: 24
    height: 24
    scale: size / 24
    transformOrigin: Item.TopLeft
    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        fillColor: glyph.color
        strokeColor: "transparent"
        PathSvg { path: glyph.path }
    }
}
