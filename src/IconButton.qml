import QtQuick

// A round icon button. It dips when pressed and springs back with a bounce.
Item {
    id: button
    property string path: ""
    property real size: 22
    property real hit: 44
    property color tint: Glass.ink
    property bool filled: false
    property color fill: Glass.ink
    // A small dot under the icon, for toggles that are on (shuffle, repeat).
    property bool marked: false
    // -1 for a plain icon; 0 or 1 to show the play/pause glyph that morphs between the two (1 = playing).
    property int playState: -1
    readonly property bool hovered: tap.containsMouse
    signal clicked()
    width: hit
    height: hit
    // A hydraulic squeeze to 0.92 when pressed, a lift to 1.06 under the pointer, both on a spring.
    scale: tap.pressed ? 0.92 : (tap.containsMouse ? 1.06 : 1)
    Behavior on scale {
        SpringAnimation { spring: Glass.springSnap; damping: Glass.dampingSnap; epsilon: 0.002 }
    }
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: button.filled ? button.fill : Glass.fill
        border.color: button.filled ? "transparent" : Glass.specular
        border.width: button.filled ? 0 : 1
        opacity: button.filled ? 1 : (tap.containsMouse || tap.pressed ? 1 : 0)
        Behavior on opacity { NumberAnimation { duration: 160 } }
    }
    Glyph {
        visible: button.playState < 0
        x: (button.width - button.size) / 2
        y: (button.height - button.size) / 2
        path: button.path
        size: button.size
        color: button.tint
    }
    PlayPauseGlyph {
        visible: button.playState >= 0
        x: (button.width - button.size) / 2
        y: (button.height - button.size) / 2
        size: button.size
        color: button.tint
        playing: button.playState === 1
    }
    Rectangle {
        visible: button.marked
        anchors.horizontalCenter: parent.horizontalCenter
        y: (button.height + button.size) / 2 + 3
        width: 4
        height: 4
        radius: 2
        color: button.tint
    }
    MouseArea {
        id: tap
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: button.clicked()
    }
}
