import QtQuick
import QtQuick.Controls

// The acrylic sheet under the "+" pill.
//
//   Liked Songs             a heart that beats while the song is liked
//   Add to Playlist         opens a list of the person's OWN playlists, inline (never Apple's own)
//   Add to "name"           only while the queue was started from one of their playlists
//   New Playlist...         an inline field, with a made-up title as its placeholder
//
// The sheet drops in with a cubic ease-out and a small overshoot (see `reveal`).
GlassPanel {
    id: sheet

    property bool open: false
    // True while the new-playlist field has the keyboard: single-letter shortcuts must stand down.
    readonly property bool typing: open && newField.activeFocus
    signal dismissed()

    radius: 22
    width: 252
    height: Math.min(menu.implicitHeight + 12, 360)
    Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    // 0 closed, 1 open. The overshoot lives in the easing (OutBack), the fade in the opacity.
    property real reveal: open ? 1 : 0
    Behavior on reveal {
        PropertyAnimation {
            duration: sheet.open ? 360 : 160
            easing.type: sheet.open ? Easing.OutBack : Easing.OutCubic
            easing.overshoot: 1.5
        }
    }
    opacity: Math.min(1, reveal * 1.6)
    visible: reveal > 0.01
    transformOrigin: Item.TopRight
    scale: 0.9 + 0.1 * reveal
    transform: Translate { y: (sheet.reveal - 1) * 18 }

    property string expanded: ""   // "", "playlists" or "new"
    Component.onCompleted: if (open && backend.previewPanel === "plus") expanded = "playlists"
    onOpenChanged: {
        if (open) {
            // CLICK_PREVIEW_PANEL=plus shows the menu with the playlist list unfolded.
            expanded = backend.previewPanel === "plus" ? "playlists" : ""
            newField.text = ""
        } else {
            newField.focus = false
        }
    }

    readonly property bool hasTrack: backend.track !== ""
    readonly property bool isLiked: backend.liked || backend.songRating > 0

    function createFromField() {
        const typed = newField.text.trim()
        backend.createPlaylist(typed)
        sheet.dismissed()
    }

    MouseArea { anchors.fill: parent }   // taps inside the sheet stay inside it

    component MenuRow: Item {
        id: row
        property string label: ""
        property string path: ""
        property bool trailingChevron: false
        property bool chevronOpen: false
        property bool enabledRow: true
        property bool beating: false
        property color glyphColor: Glass.ink
        signal clicked()
        width: parent ? parent.width : 0
        height: 44
        opacity: enabledRow ? 1 : 0.4
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 14
            color: rowTap.pressed ? Glass.fillHover : (rowTap.containsMouse && row.enabledRow ? Glass.fill : "transparent")
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        Glyph {
            id: rowGlyph
            x: 16
            y: (parent.height - size) / 2
            size: 20
            path: row.path
            color: row.glyphColor
            transformOrigin: Item.Center
            SequentialAnimation on scale {
                running: row.beating && sheet.visible
                loops: Animation.Infinite
                NumberAnimation { to: 1.22; duration: 160; easing.type: Easing.OutCubic }
                NumberAnimation { to: 1.0; duration: 200; easing.type: Easing.InCubic }
                NumberAnimation { to: 1.12; duration: 140; easing.type: Easing.OutCubic }
                NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
            }
        }
        Text {
            x: 48
            width: parent.width - 48 - 40
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            elide: Text.ElideRight
            color: Glass.ink
            font.family: Glass.font
            font.pixelSize: 14
            font.weight: Font.Medium
        }
        Glyph {
            visible: row.trailingChevron
            x: parent.width - 32
            y: (parent.height - size) / 2
            size: 16
            color: Glass.faint
            path: "M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6z"
            rotation: row.chevronOpen ? 90 : 0
            transformOrigin: Item.Center
            Behavior on rotation { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        }
        MouseArea {
            id: rowTap
            anchors.fill: parent
            hoverEnabled: true
            enabled: row.enabledRow
            cursorShape: Qt.PointingHandCursor
            onClicked: row.clicked()
        }
    }

    Flickable {
        id: scroller
        x: 6
        y: 6
        width: parent.width - 12
        height: parent.height - 12
        contentHeight: menu.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: menu
            width: parent.width

            MenuRow {
                label: sheet.isLiked ? qsTr("Liked") : qsTr("Liked Songs")
                path: "M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z"
                glyphColor: sheet.isLiked ? "#ff5a7a" : Glass.ink
                beating: sheet.isLiked
                enabledRow: sheet.hasTrack
                onClicked: backend.toggleLike()
            }

            MenuRow {
                label: qsTr("Add to Playlist")
                path: "M14 10H2v2h12v-2zm0-4H2v2h12V6zm4 8v-4h-2v4h-4v2h4v4h2v-4h4v-2h-4zM2 16h8v-2H2v2z"
                trailingChevron: true
                chevronOpen: sheet.expanded === "playlists"
                enabledRow: sheet.hasTrack
                onClicked: {
                    sheet.expanded = sheet.expanded === "playlists" ? "" : "playlists"
                    if (sheet.expanded === "playlists") backend.refreshPlaylists()
                }
            }
            // Inline sub-list: only the person's own playlists (the page already dropped Apple's).
            Item {
                width: parent.width
                height: sheet.expanded === "playlists" ? Math.min(160, Math.max(36, listColumn.implicitHeight)) : 0
                clip: true
                Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                Flickable {
                    anchors.fill: parent
                    contentHeight: listColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Column {
                        id: listColumn
                        width: parent.width
                        Text {
                            visible: backend.playlists.length === 0
                            x: 48
                            width: parent.width - 60
                            height: 36
                            verticalAlignment: Text.AlignVCenter
                            text: backend.playlistsLoading ? qsTr("Loading…") : qsTr("No playlists of your own yet")
                            color: Glass.faint
                            font.family: Glass.font
                            font.pixelSize: 12
                        }
                        Repeater {
                            model: backend.playlists
                            Item {
                                id: plRow
                                required property var modelData
                                width: listColumn.width
                                height: 36
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.leftMargin: 34
                                    anchors.margins: 2
                                    radius: 12
                                    color: plTap.pressed ? Glass.fillHover : (plTap.containsMouse ? Glass.fill : "transparent")
                                }
                                Text {
                                    x: 48
                                    width: parent.width - 64
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: plRow.modelData.name
                                    elide: Text.ElideRight
                                    color: Glass.ink
                                    font.family: Glass.font
                                    font.pixelSize: 13
                                }
                                MouseArea {
                                    id: plTap
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        backend.addToPlaylist(plRow.modelData.id, plRow.modelData.name)
                                        sheet.dismissed()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            MenuRow {
                visible: backend.currentPlaylist !== ""
                height: visible ? 44 : 0
                label: qsTr("Add to “%1”").arg(backend.currentPlaylist)
                path: "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z"
                enabledRow: sheet.hasTrack
                onClicked: {
                    backend.addToCurrentPlaylist()
                    sheet.dismissed()
                }
            }

            MenuRow {
                label: qsTr("New Playlist…")
                path: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"
                trailingChevron: true
                chevronOpen: sheet.expanded === "new"
                enabledRow: sheet.hasTrack
                onClicked: {
                    sheet.expanded = sheet.expanded === "new" ? "" : "new"
                    if (sheet.expanded === "new") Qt.callLater(function () { newField.forceActiveFocus() })
                }
            }
            Item {
                width: parent.width
                height: sheet.expanded === "new" ? 52 : 0
                clip: true
                Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                Rectangle {
                    x: 12
                    y: 4
                    width: parent.width - 24
                    height: 40
                    radius: 20
                    color: "#40000000"
                    border.width: 1
                    border.color: newField.activeFocus ? Glass.accentLight : Glass.specular
                    Behavior on border.color { ColorAnimation { duration: 140 } }
                    TextInput {
                        id: newField
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 40
                        verticalAlignment: TextInput.AlignVCenter
                        color: Glass.ink
                        font.family: Glass.font
                        font.pixelSize: 14
                        selectByMouse: true
                        clip: true
                        maximumLength: 60
                        onAccepted: sheet.createFromField()
                        Keys.onEscapePressed: sheet.dismissed()
                        Text {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            visible: newField.text.length === 0
                            // A different made-up title for every song, so "just press Enter" is a fine answer.
                            text: backend.suggestPlaylistName(backend.track + backend.artist)
                            color: Glass.faint
                            font: newField.font
                            elide: Text.ElideRight
                        }
                    }
                    Glyph {
                        x: parent.width - 30
                        y: (parent.height - size) / 2
                        size: 18
                        color: Glass.ink
                        path: "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z"
                        MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: sheet.createFromField() }
                    }
                }
            }
        }
    }
}
