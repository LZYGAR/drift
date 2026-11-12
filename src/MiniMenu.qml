import QtQuick
import QtQuick.Controls

// A menu for the mini player, in a window of its own (a Wayland popup), so it can be much bigger than the
// 144 px mini without the mini changing size or moving. The compositor keeps it on screen: below the mini, or
// above it when the mini sits at the bottom. It brings its own colour mesh, so the liquid glass has the song's
// colours behind it like everywhere else.
Popup {
    id: menu

    default property alias content: holder.data
    property real menuWidth: 300
    property real menuHeight: 300

    popupType: Popup.Window
    modal: false
    focus: true
    padding: 0
    width: menuWidth
    height: menuHeight
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Item {}

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110; easing.type: Easing.InCubic }
    }

    contentItem: Item {
        id: frame
        MeshBackground {
            id: mesh
            anchors.fill: parent
            running: menu.visible && backend.playing
            calm: 0
            visible: false
        }
        LiquidGlass {
            anchors.fill: parent
            radius: 22
            source: mesh
            backX: 0
            backY: 0
            shadow: false
            live: menu.visible
            tint: 0.3
        }
        Item {
            id: holder
            anchors.fill: parent
        }
    }
}
