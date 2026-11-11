import QtQuick

// A liquid on/off switch.
//
// The thumb travels on a spring and, as it sets off, stretches in the
// direction it is going (to 1.25 times its width) before snapping back to a
// circle with a high-tension spring, like a drop of liquid being flicked. When
// on, the track glows softly in the cover's colour with a specular sheen.
// Pressing squeezes the thumb a little.
Item {
    id: toggle

    property bool checked: false
    property bool interactive: true
    signal toggled()

    implicitWidth: 52
    implicitHeight: 30
    width: implicitWidth
    height: implicitHeight
    opacity: interactive ? 1 : 0.4

    // A soft glow around the track when on.
    Rectangle {
        anchors.centerIn: parent
        width: parent.width + 10
        height: parent.height + 10
        radius: height / 2
        color: Glass.accent
        opacity: toggle.checked ? 0.22 : 0
        Behavior on opacity { NumberAnimation { duration: 220 } }
    }
    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: toggle.checked ? Glass.accent : "#2effffff"
        border.color: toggle.checked ? Qt.lighter(Glass.accent, 1.35) : Glass.rimMiddle
        border.width: 1
        Behavior on color { ColorAnimation { duration: 200 } }
        // A sheen along the top of the track.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0; color: toggle.checked ? "#55ffffff" : "#22ffffff" }
                GradientStop { position: 0.55; color: "transparent" }
            }
        }
    }
    Rectangle {
        id: thumb
        y: 3
        width: 24
        height: 24
        radius: 12
        color: Glass.ink
        x: toggle.checked ? toggle.width - width - 3 : 3
        Behavior on x { SpringAnimation { spring: Glass.springSnap; damping: Glass.dampingSnap; epsilon: 0.05 } }
        // The stretch: towards where it is going, then back to round.
        property real stretch: 1
        transform: Scale {
            origin.x: toggle.checked ? 0 : thumb.width
            origin.y: thumb.height / 2
            xScale: thumb.stretch * (press.pressed ? 0.92 : 1)
            yScale: (2 - thumb.stretch) * (press.pressed ? 0.92 : 1)
        }
        SequentialAnimation {
            id: flick
            NumberAnimation { target: thumb; property: "stretch"; to: 1.25; duration: 90; easing.type: Easing.OutQuad }
            SpringAnimation { target: thumb; property: "stretch"; to: 1; spring: Glass.springSnap; damping: Glass.dampingSnap; epsilon: 0.002 }
        }
        // A highlight on the thumb, as on a real bead of glass.
        Rectangle {
            x: 4; y: 3
            width: 12; height: 7
            radius: 4
            color: "#66ffffff"
        }
    }
    onCheckedChanged: flick.restart()
    MouseArea {
        id: press
        anchors.fill: parent
        enabled: toggle.interactive
        cursorShape: Qt.PointingHandCursor
        onClicked: toggle.toggled()
    }
}
