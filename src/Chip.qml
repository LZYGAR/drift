import QtQuick

// A rounded pill button: a label, filled with ink when it is the main action.
Rectangle {
    id: chip
    property string label: ""
    property bool strong: false     // filled with ink, for the main action
    property bool tinted: false     // a touch of the accent, for chips made for this moment
    property bool dimmed: false
    property real pad: 14
    signal clicked()
    height: 34
    width: chipText.implicitWidth + pad * 2
    radius: height / 2
    color: strong ? Glass.ink
        : tinted ? Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, chipArea.containsMouse ? 0.42 : 0.26)
        : (chipArea.containsMouse ? Glass.fillHover : Glass.fill)
    border.color: strong ? "transparent" : Glass.specular
    border.width: 1
    opacity: dimmed ? 0.45 : 1
    scale: chipArea.pressed ? 0.94 : 1
    Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
    Behavior on color { ColorAnimation { duration: 140 } }
    Behavior on opacity { NumberAnimation { duration: 160 } }
    Text {
        id: chipText
        anchors.centerIn: parent
        text: chip.label
        color: chip.strong ? Glass.inkText : Glass.ink
        font.family: Glass.font
        font.pixelSize: 13
        font.weight: chip.strong ? Font.Bold : Font.Normal
    }
    MouseArea {
        id: chipArea
        anchors.fill: parent
        hoverEnabled: true
        enabled: !chip.dimmed
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.clicked()
    }
}
