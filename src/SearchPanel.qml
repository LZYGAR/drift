import QtQuick

// Search as a menu: a field, recent searches, and the results, which open artists and albums in place
// (with a back arrow). Used where the full player's library is not on show: full screen and the mini player.
// It drives the same backend screens as the library, so everything works the same way.
GlassPanel {
    id: panel

    property bool open: false
    signal closeRequested()

    radius: 22
    clip: true

    // Picking a song plays it, and the menu has done its job.
    property string startKey: ""
    readonly property string trackKey: backend.track + "\u0000" + backend.artist
    onTrackKeyChanged: if (open && startKey !== "" && trackKey !== startKey) closeRequested()

    onOpenChanged: {
        if (open) {
            startKey = trackKey === "\u0000" ? "-" : trackKey
            backend.openSearch()
            field.text = ""
            field.forceActiveFocus()
        } else {
            field.focus = false
            // Back to wherever the menus were before: the song, or the top.
            for (let i = 0; i < 20 && backend.kind !== "now" && !backend.atRoot; ++i)
                backend.menu()
        }
    }

    readonly property bool inSearch: backend.kind === "search"
    // Small while nothing is typed (the field and recent searches), tall once there are results.
    readonly property bool idle: inSearch && field.text.length === 0
    readonly property real wantHeight: idle ? recent.y + (recent.visible ? recent.height : 0) + 14 : 560

    // ---- the field, or the screen's title with a back arrow ----
    Item {
        id: head
        x: 12
        y: 12
        width: parent.width - 24
        height: 40
        Rectangle {
            anchors.fill: parent
            radius: 14
            color: Glass.fill
            border.color: field.activeFocus ? Glass.accentLight : Glass.specular
            border.width: 1
            visible: panel.inSearch
            Glyph {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                size: 16
                color: Glass.mute
                path: "M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"
            }
            TextInput {
                id: field
                x: 36
                width: parent.width - 48
                anchors.verticalCenter: parent.verticalCenter
                color: Glass.ink
                selectionColor: Glass.accent
                font.family: Glass.font
                font.pixelSize: 15
                clip: true
                onTextChanged: if (panel.inSearch) backend.setSearch(text)
                Keys.onEscapePressed: panel.closeRequested()
                Keys.onDownPressed: backend.wheel(1)
                Keys.onUpPressed: backend.wheel(-1)
                Keys.onReturnPressed: backend.center()
                Text {
                    visible: field.text.length === 0
                    text: qsTr("Songs, artists, albums")
                    color: Glass.faint
                    font: field.font
                }
            }
        }
        // a deeper screen (an artist, an album): its title and the way back
        Row {
            visible: !panel.inSearch
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            IconButton {
                path: "M15.41 7.41L14 6l-6 6 6 6 1.41-1.41L10.83 12z"
                size: 22
                hit: 36
                onClicked: backend.menu()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: head.width - 44
                text: backend.title
                color: Glass.ink
                elide: Text.ElideRight
                font.family: Glass.font
                font.pixelSize: 16
                font.weight: Font.Bold
            }
        }
    }

    // ---- recent searches, while nothing is typed ----
    Flow {
        id: recent
        x: 12
        y: head.y + head.height + 10
        width: parent.width - 24
        spacing: 6
        visible: panel.inSearch && field.text.length === 0
        Repeater {
            model: recent.visible ? backend.searchHistory : []
            Chip {
                required property string modelData
                label: modelData
                height: 30
                onClicked: field.text = modelData
            }
        }
    }

    // ---- the rows ----
    ListView {
        id: rows
        x: 6
        y: head.y + head.height + 8
        width: parent.width - 12
        height: parent.height - y - 8
        visible: !recent.visible
        clip: true
        model: backend.rows
        currentIndex: backend.selected
        boundsBehavior: Flickable.StopAtBounds
        spacing: 2
        highlightMoveDuration: 140
        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            width: rows.width
            height: 48
            radius: 12
            color: rowTap.containsMouse || index === backend.selected ? Glass.fill : "transparent"
            Behavior on color { ColorAnimation { duration: 110 } }
            Column {
                x: 12
                width: parent.width - 24
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Text {
                    width: parent.width
                    text: row.modelData.label
                    color: Glass.ink
                    elide: Text.ElideRight
                    font.family: Glass.font
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    visible: text !== ""
                    text: row.modelData.detail || ""
                    color: Glass.mute
                    elide: Text.ElideRight
                    font.family: Glass.font
                    font.pixelSize: 12
                }
            }
            MouseArea {
                id: rowTap
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    backend.wheel(row.index - backend.selected)
                    backend.center()
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: rows.count === 0 && field.text.length > 0
            text: backend.status !== "" ? backend.status : qsTr("Nothing found")
            color: Glass.mute
            font.family: Glass.font
            font.pixelSize: 13
        }
    }
}
