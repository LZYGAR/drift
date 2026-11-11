import QtQuick

// The "+" in the header: a small frosted-glass pill that opens the action menu.
//
// Idle     a faint glass disc with a 1 px specular rim (bright top-left, as everywhere in Click).
// Hover    scales to 1.12 on the pill spring (Glass.springPill / dampingPill, see Glass.qml for the
//          stiffness 450 / damping 22 conversion) and a luminous ring flares around the rim.
// Open     the plus turns a quarter into a cross, so the same button says "close".
Item {
    id: pill
    property bool open: false
    signal clicked()
    width: 32
    height: 32

    readonly property bool hovered: tap.containsMouse
    scale: tap.pressed ? 0.94 : (hovered ? 1.12 : 1)
    Behavior on scale { SpringAnimation { spring: Glass.springPill; damping: Glass.dampingPill; mass: 1; epsilon: 0.001 } }

    // The flare: a ring one pixel outside the rim, lit with the accent.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: Glass.accentLight
        opacity: pill.hovered || pill.open ? 0.55 : 0
        scale: pill.hovered ? 1.0 : 0.88
        Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on scale { SpringAnimation { spring: Glass.springPill; damping: Glass.dampingPill; epsilon: 0.001 } }
    }
    Rectangle {
        id: disc
        anchors.fill: parent
        radius: width / 2
        color: pill.open ? Glass.fillHover : (pill.hovered ? Glass.fillHover : Glass.fill)
        border.width: 1
        border.color: Glass.specular
        Behavior on color { ColorAnimation { duration: 160 } }
    }
    // The glyph scales from its top-left corner, so it is turned inside a plain box that is centred exactly.
    Item {
        id: turn
        anchors.centerIn: parent
        width: 16
        height: 16
        rotation: pill.open ? 135 : 0
        Behavior on rotation { SpringAnimation { spring: Glass.springPill; damping: Glass.dampingPill; epsilon: 0.1 } }
        Glyph {
            size: 16
            color: Glass.ink
            path: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"
        }
    }
    MouseArea {
        id: tap
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: pill.clicked()
    }
}
