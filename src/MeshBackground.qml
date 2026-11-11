import QtQuick
import QtQuick.Shapes

// The backdrop: a mesh of soft colour fields taken from the cover.
//
// Four radial gradients (primary, secondary, a lighter primary and the dark
// ambient tint) sit on the ambient base and drift on slow, unrelated orbits,
// so the mesh never visibly repeats. The fields are drawn by the GPU as plain
// gradients; nothing is blurred, so the cost is a few moving quads.
//
// Colours ease over Glass.colorShift (800 ms) when the song changes, and the
// fields swell once ("breathe") as they do. The drift steps on the shared
// 20 Hz clock (Glass.beat) and only while `running` is true; each step moves a
// field well under a pixel, so it still reads as continuous motion.
Item {
    id: mesh

    property color primary: Glass.accent
    property color secondary: Glass.secondary
    property color ambient: Glass.ambient
    // Drift only while music plays. A paused player redraws nothing.
    property bool running: false
    // 0 is full colour for the player. 1 is calmer, behind lists, so text stays easy to read.
    property real calm: 0
    Behavior on calm { NumberAnimation { duration: 600; easing.type: Easing.InOutSine } }

    // Flow time, advanced every drawn frame while music plays, faster when it is loud.
    property real phase: 0
    FrameAnimation {
        running: mesh.running && mesh.visible
        onTriggered: mesh.phase += frameTime * (0.05 + (Glass.reactive ? 0.35 * Glass.level + 0.2 * Glass.mid : 0))
    }

    // How hard the music is hitting right now, 0 to 1. The flow churns and
    // brightens with every kick.
    readonly property real pump: Glass.reactive ? Math.min(1, Glass.kick * 0.7 + Glass.bass * 0.4) * (1 - 0.7 * calm) : 0

    // 0 at rest, rising to 1 and back over 800 ms when breathe() is called.
    property real breath: 0
    function breathe() { swell.restart() }
    SequentialAnimation {
        id: swell
        NumberAnimation { target: mesh; property: "breath"; to: 1; duration: Glass.colorShift / 2; easing.type: Easing.InOutSine }
        NumberAnimation { target: mesh; property: "breath"; to: 0; duration: Glass.colorShift / 2; easing.type: Easing.InOutSine }
    }

    readonly property real span: Math.max(width, height)

    // One GPU pass: the cover's colours poured through warped noise (src/shaders/mesh.frag).
    ShaderEffect {
        anchors.fill: parent
        opacity: 1 - 0.5 * mesh.calm
        property color primary: mesh.primary
        property color secondary: mesh.secondary
        property color ambient: mesh.ambient
        property color base: Glass.mix(mesh.ambient, Glass.base, 0.35)
        property size size: Qt.size(width, height)
        property real time: mesh.phase
        property real pump: mesh.pump
        property real calm: mesh.calm
        property real breath: mesh.breath
        fragmentShader: Qt.resolvedUrl("shaders/mesh.frag.qsb")
    }
    // A soft flash of the main colour on every beat.
    Rectangle {
        anchors.fill: parent
        color: Qt.lighter(mesh.primary, 1.25)
        opacity: 0.2 * mesh.pump
    }
    // Darker towards the bottom, where the controls and long lists sit.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#1f000000" }
            GradientStop { position: 0.5; color: "#47000000" }
            GradientStop { position: 1; color: "#a3000000" }
        }
    }
}
