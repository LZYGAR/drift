import QtQuick
import QtQuick.Effects

// The liquid glass of the full-screen controls, for anything that floats over a moving backdrop: what is behind it,
// blurred and a little richer, the same liquid moving inside it, a smoky tint so white text always reads, a gloss
// along the top, and a rim that lights up with the bass. Give it `source` (the backdrop) and where it sits in it.
Item {
    id: glass

    property Item source: null
    property real backX: x
    property real backY: y
    property real radius: 22
    property bool live: true
    property bool shadow: true
    property real tint: 0.22

    Rectangle { id: mask; anchors.fill: parent; radius: glass.radius; visible: false; layer.enabled: true }
    // A soft shadow so it floats.
    Rectangle {
        visible: glass.shadow
        anchors.fill: parent
        radius: glass.radius
        color: "black"
        layer.enabled: visible
        layer.effect: MultiEffect { shadowEnabled: true; shadowColor: "black"; shadowOpacity: 0.3; shadowBlur: 1.0; blurMax: 48; shadowVerticalOffset: 12; autoPaddingEnabled: true }
    }
    ShaderEffectSource {
        id: grab
        anchors.fill: parent
        visible: false
        sourceItem: glass.source
        sourceRect: Qt.rect(glass.backX, glass.backY, glass.width, glass.height)
        live: glass.live
        textureSize: Qt.size(Math.max(1, Math.ceil(glass.width / 2)), Math.max(1, Math.ceil(glass.height / 2)))
    }
    MultiEffect {
        anchors.fill: parent
        visible: glass.source !== null
        source: grab
        blurEnabled: true
        blur: 1.0
        blurMax: 48
        saturation: 0.35
        autoPaddingEnabled: false
        maskEnabled: true
        maskSource: mask
    }
    // The same liquid as the backdrop, moving inside the glass.
    Item {
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect { maskEnabled: true; maskSource: mask }
        LiquidField {
            anchors.fill: parent
            running: glass.live && glass.visible
            pace: 0.8
            opacity: 0.45
        }
    }
    Rectangle { anchors.fill: parent; radius: glass.radius; color: Qt.rgba(0, 0, 0, glass.tint) }
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#2effffff" }
            GradientStop { position: 0.4; color: "transparent" }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: "transparent"
        border.width: 1
        border.color: "#38ffffff"
    }
    // The rim lights up with the bass.
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: "transparent"
        border.width: 1.5
        border.color: Glass.accentLight
        opacity: 0.12 + 0.5 * (Glass.reactive ? Math.min(1, Glass.cover) : 0)
    }
}
