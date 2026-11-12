import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// A pane of liquid glass.
//
// Layers, back to front:
//   shadow      deep and soft (32 px of blur, 24 px down), so the pane floats
//   frost       whatever is behind it, blurred and a little more saturated
//   shade       a light dark tint, so text stays readable over any cover
//   milk        8% white: the glass itself
//   light spill a faint glow of the cover's colour from the top-left corner
//   gloss       a sheen along the top edge
//   rim         a 1px edge that is bright at the top-left and fades towards the
//               bottom-right, as if lit from above
//
// Give it `source` (the item drawn behind it) to get the live blur. The blur
// only runs while the panel is visible, and its texture is freed when hidden.
// The rim is drawn once into a texture; nothing here changes per frame.
Item {
    id: panel

    property Item source: null
    // Where the panel sits in `source` coordinates. The defaults are right
    // when the panel and the source share a parent and the source starts at 0,0.
    property real backX: x
    property real backY: y
    property real radius: Glass.panelRadius
    property color shade: Glass.panelShade
    property bool shadow: true
    // A faint glow of the accent colour in the top-left corner, like light from the cover.
    property bool lightSpill: true
    default property alias content: inner.data
    // Bare: no glass of its own, only the content. For panels that sit on another glass (LiquidGlass).
    property bool bare: false

    readonly property bool frosted: !bare && source !== null && visible && width > 0 && height > 0

    // ---- shadow ----
    Item {
        id: shadowShape
        anchors.fill: parent
        visible: false
        Rectangle { anchors.fill: parent; radius: panel.radius; color: "black" }
    }
    MultiEffect {
        visible: panel.shadow && !panel.bare
        anchors.fill: shadowShape
        source: shadowShape
        shadowEnabled: true
        shadowColor: "black"
        shadowOpacity: Glass.shadowOpacity
        shadowBlur: 1.0
        shadowVerticalOffset: Glass.shadowOffset
        shadowScale: 0.96
        blurMax: Glass.shadowBlur
        autoPaddingEnabled: true
    }

    // ---- frost ----
    ShaderEffectSource {
        id: grab
        anchors.fill: parent
        visible: false
        sourceItem: panel.frosted ? panel.source : null
        sourceRect: Qt.rect(panel.backX, panel.backY, panel.width, panel.height)
        live: panel.frosted
        // Half resolution: it is about to be blurred anyway.
        textureSize: Qt.size(Math.max(1, Math.ceil(panel.width / 2)), Math.max(1, Math.ceil(panel.height / 2)))
    }
    Rectangle {
        id: shape
        anchors.fill: parent
        radius: panel.radius
        visible: false
        layer.enabled: panel.frosted
    }
    MultiEffect {
        anchors.fill: parent
        visible: panel.frosted
        source: grab
        blurEnabled: true
        blur: 1.0
        blurMax: Glass.panelBlur
        saturation: Glass.panelSaturation
        autoPaddingEnabled: false
        maskEnabled: true
        maskSource: shape
    }
    // Without a source there is nothing to blur, so the base colour stands in.
    Rectangle {
        anchors.fill: parent
        radius: panel.radius
        visible: !panel.frosted && !panel.bare
        color: Glass.base
    }

    // ---- shade, milk, light spill, gloss ----
    Rectangle {
        anchors.fill: parent
        visible: !panel.bare
        radius: panel.radius
        color: panel.shade
    }
    Rectangle {
        anchors.fill: parent
        visible: !panel.bare
        radius: panel.radius
        color: Glass.glassMilk
    }
    Shape {
        anchors.fill: parent
        visible: panel.lightSpill && !panel.bare
        layer.enabled: visible
        layer.effect: MultiEffect { maskEnabled: true; maskSource: shape }
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 0; centerY: 0
                focalX: 0; focalY: 0
                centerRadius: Math.max(panel.width, panel.height) * 0.75
                GradientStop { position: 0; color: Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.16) }
                GradientStop { position: 1; color: "transparent" }
            }
            startX: 0; startY: 0
            PathLine { x: panel.width; y: 0 }
            PathLine { x: panel.width; y: panel.height }
            PathLine { x: 0; y: panel.height }
            PathLine { x: 0; y: 0 }
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: !panel.bare
        radius: panel.radius
        gradient: Gradient {
            GradientStop { position: 0; color: Glass.sheen }
            GradientStop { position: 0.35; color: "transparent" }
        }
    }

    // ---- rim: bright top-left, fading to the bottom-right ----
    Item {
        id: rimRing
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle {
            anchors.fill: parent
            radius: panel.radius
            color: "transparent"
            border.color: "white"
            border.width: 1
        }
    }
    Shape {
        anchors.fill: parent
        visible: !panel.bare
        layer.enabled: visible
        layer.effect: MultiEffect { maskEnabled: true; maskSource: rimRing }
        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 0
                x2: panel.width; y2: panel.height
                GradientStop { position: 0; color: Glass.rimBright }
                GradientStop { position: 0.45; color: Glass.rimMiddle }
                GradientStop { position: 1; color: Glass.rimDim }
            }
            startX: 0; startY: 0
            PathLine { x: panel.width; y: 0 }
            PathLine { x: panel.width; y: panel.height }
            PathLine { x: 0; y: panel.height }
            PathLine { x: 0; y: 0 }
        }
    }

    Item {
        id: inner
        anchors.fill: parent
    }
}
