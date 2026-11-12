import QtQuick

// Settings and Wrapped on one glass sheet. Used by the full player, the mini player's menu and full screen.
GlassPanel {
    id: sheet

    property bool open: false
    property string tab: "general"
    property string visualStyle: "liquid"
    property var visualStyles: []
    property bool showFollow: false
    // The Wrapped tab scrolls inside this height.
    property real maxHeight: 520
    signal styleChosen(string id)

    // One compact setting: a name, one quiet line under it, and a switch.
    component SettingRow: Item {
        id: setting
        property string title: ""
        property string note: ""
        property bool checked: false
        property bool interactive: true
        signal toggled()
        width: parent ? parent.width : 0
        height: 58
        Rectangle { width: parent.width - 12; x: 6; height: 1; color: Glass.specular }
        Text {
            x: 12
            y: 10
            text: setting.title
            color: Glass.ink
            font.family: Glass.font
            font.pixelSize: 15
            font.weight: setting.checked ? Font.DemiBold : Font.Normal
        }
        Text {
            x: 12
            y: 32
            width: parent.width - 84
            text: setting.note
            color: Glass.mute
            elide: Text.ElideRight
            font.family: Glass.font
            font.pixelSize: 11
        }
        LiquidSwitch {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 13
            checked: setting.checked
            interactive: setting.interactive
            onToggled: setting.toggled()
        }
    }

    component SmallLabel: Text {
        color: Glass.faint
        font.family: Glass.font
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }

    readonly property real wantHeight: 52 + (sheet.tab === "general" ? generalTab.height : Math.min(wrappedFlick.contentHeight, sheet.maxHeight - 52 - 8)) + 8
    height: wantHeight
    Behavior on height { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
    radius: 22
    opacity: sheet.open ? 1 : 0
    scale: sheet.open ? 1 : 0.9
    transformOrigin: Item.TopLeft
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: 160 } }
    Behavior on scale { SpringAnimation { spring: Glass.springPanel; damping: Glass.dampingPanel; epsilon: 0.002 } }
    MouseArea { anchors.fill: parent }

    // two tabs, one sheet
    Row {
        x: 16
        y: 12
        spacing: 8
        Chip {
            label: qsTr("Settings")
            height: 30
            strong: sheet.tab === "general"
            onClicked: sheet.tab = "general"
        }
        Chip {
            label: qsTr("Wrapped")
            height: 30
            strong: sheet.tab === "wrapped"
            onClicked: sheet.tab = "wrapped"
        }
    }

    // ---- Settings tab ----
    Column {
        id: generalTab
        x: 8
        y: 52
        width: parent.width - 16
        opacity: sheet.tab === "general" ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 160 } }

        Item {
            width: parent.width
            height: animFlow.implicitHeight + 62
            SmallLabel { x: 12; y: 2; text: qsTr("Animation") }
            Flow {
                id: animFlow
                x: 8
                y: 24
                width: parent.width - 16
                spacing: 8
                Repeater {
                    model: sheet.visualStyles
                    Chip {
                        required property var modelData
                        label: modelData.label
                        strong: sheet.visualStyle === modelData.id
                        onClicked: sheet.styleChosen(modelData.id)
                    }
                }
            }
            Text {
                x: 12
                y: animFlow.y + animFlow.implicitHeight + 8
                width: parent.width - 24
                text: {
                    for (const style of sheet.visualStyles)
                        if (style.id === sheet.visualStyle)
                            return style.hint
                    return ""
                }
                color: Glass.mute
                elide: Text.ElideRight
                font.family: Glass.font
                font.pixelSize: 11
            }
        }
        SettingRow {
            title: qsTr("Play similar songs")
            note: !backend.similarAvailable ? qsTr("Needs the Grok CLI.")
                : backend.similarStatus !== "" ? backend.similarStatus
                : qsTr("Keeps up to 10 songs queued, like the one you picked")
            checked: backend.autoSimilar
            interactive: backend.similarAvailable
            onToggled: backend.autoSimilar = !backend.autoSimilar
        }
        SettingRow {
            title: qsTr("Learn what I like")
            note: backend.useTaste ? backend.tasteSummary : qsTr("Off. Nothing is remembered.")
            checked: backend.useTaste
            onToggled: backend.useTaste = !backend.useTaste
        }
        SettingRow {
            title: qsTr("Opening animation")
            note: backend.introEnabled ? qsTr("The word draws itself as Drift opens") : qsTr("Off. Drift opens straight to your music")
            checked: backend.introEnabled
            onToggled: backend.introEnabled = !backend.introEnabled
        }
        SettingRow {
            title: qsTr("Follow Omarchy theme")
            note: backend.themeFollow
                ? (theme.light ? qsTr("Light themes keep Drift's dark glass; the accent still follows")
                               : qsTr("Colors, text and idle look come from your theme; covers still lead"))
                : qsTr("Off. Drift uses its own black and white")
            checked: backend.themeFollow
            onToggled: backend.themeFollow = !backend.themeFollow
        }
        SettingRow {
            title: qsTr("Use Omarchy font")
            note: theme.fontFamily === "" ? qsTr("Omarchy's font was not found. Drift keeps Inter")
                : backend.fontFollow ? theme.fontFamily : qsTr("Off. Drift uses Inter")
            checked: backend.fontFollow
            interactive: theme.fontFamily !== ""
            onToggled: backend.fontFollow = !backend.fontFollow
        }
        SettingRow {
            visible: sheet.showFollow
            height: visible ? 58 : 0
            title: qsTr("Follow me across desktops")
            note: qsTr("Carries the player along as the mini player")
            checked: desktop.follow
            onToggled: desktop.follow = !desktop.follow
        }
    }

    // ---- Wrapped tab ----
    Flickable {
        id: wrappedFlick
        x: 8
        y: 52
        width: parent.width - 16
        height: parent.height - 52 - 8
        contentWidth: width
        contentHeight: wrapped.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        opacity: sheet.tab === "wrapped" ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 160 } }

        Column {
            id: wrapped
            width: parent.width
            spacing: 10
            readonly property var stats: backend.stats
            readonly property var songs: stats.topSongs
            readonly property var artists: stats.topArtists
            readonly property var week: stats.week
            readonly property real weekMax: {
                let top = 1
                for (const day of week)
                    top = Math.max(top, day.seconds)
                return top
            }

            // empty until there is something to say
            Text {
                visible: !wrapped.stats.hasData
                width: parent.width
                topPadding: 24
                bottomPadding: 28
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Listen for a while and your Wrapped fills in here: time, favourites and streaks. It stays on this machine.")
                color: Glass.mute
                font.family: Glass.font
                font.pixelSize: 13
                leftPadding: 20
                rightPadding: 20
            }

            // the headline
            Rectangle {
                visible: wrapped.stats.hasData
                width: parent.width
                height: visible ? 92 : 0
                radius: 18
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.55) }
                    GradientStop { position: 1; color: Qt.rgba(Glass.accentLight.r, Glass.accentLight.g, Glass.accentLight.b, 0.18) }
                }
                border.width: 1
                border.color: Glass.specular
                SmallLabel { x: 16; y: 12; text: qsTr("You have listened for"); color: Glass.mute }
                Text {
                    x: 16
                    y: 30
                    text: wrapped.stats.total
                    color: Glass.ink
                    font.family: Glass.font
                    font.pixelSize: 34
                    font.weight: Font.Bold
                }
                Text {
                    x: 16
                    y: 70
                    text: qsTr("%1 today").arg(wrapped.stats.today)
                    color: Glass.mute
                    font.family: Glass.font
                    font.pixelSize: 11
                }
            }

            // three small numbers
            Row {
                visible: wrapped.stats.hasData
                width: parent.width
                height: visible ? 58 : 0
                spacing: 8
                Repeater {
                    model: [
                        { value: String(wrapped.stats.plays), name: qsTr("plays") },
                        { value: wrapped.stats.streak > 0 ? String(wrapped.stats.streak) : "–", name: qsTr("day streak") },
                        { value: wrapped.stats.peak !== "" ? wrapped.stats.peak : "–", name: qsTr("you listen most") }
                    ]
                    Rectangle {
                        required property var modelData
                        width: (wrapped.width - 16) / 3
                        height: 58
                        radius: 14
                        color: Glass.fill
                        border.width: 1
                        border.color: Glass.specular
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 9
                            width: parent.width - 8
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: parent.modelData.value
                            color: Glass.ink
                            font.family: Glass.font
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 34
                            text: parent.modelData.name
                            color: Glass.faint
                            font.family: Glass.font
                            font.pixelSize: 10
                        }
                    }
                }
            }

            // the last seven days
            Item {
                visible: wrapped.stats.hasData
                width: parent.width
                height: visible ? 64 : 0
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 8
                    Repeater {
                        model: wrapped.week
                        Item {
                            required property var modelData
                            required property int index
                            width: (wrapped.width - 12 - 6 * 8) / 7
                            height: 64
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width
                                height: Math.max(4, 42 * parent.modelData.seconds / wrapped.weekMax)
                                y: 44 - height
                                radius: 5
                                color: parent.index === 6 ? Glass.accentLight : Glass.rail
                                Behavior on height { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 49
                                text: parent.modelData.day
                                color: Glass.faint
                                font.family: Glass.font
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }

            // favourites
            SmallLabel { visible: wrapped.stats.hasData && wrapped.songs.length > 0; leftPadding: 6; text: qsTr("Most played") }
            Repeater {
                model: wrapped.stats.hasData ? wrapped.songs.slice(0, 3) : []
                Item {
                    id: songRow
                    required property var modelData
                    required property int index
                    width: wrapped.width
                    height: 44
                    Rectangle {
                        id: cover
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 38
                        height: 38
                        radius: 8
                        color: songRow.index === 0 ? Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.5) : Glass.fill
                        clip: true
                        Image {
                            id: coverImage
                            anchors.fill: parent
                            source: songRow.modelData.artwork || ""
                            visible: status === Image.Ready
                            fillMode: Image.PreserveAspectCrop
                        }
                        Text {
                            anchors.centerIn: parent
                            text: String(songRow.index + 1)
                            visible: !coverImage.visible
                            color: Glass.ink
                            font.family: Glass.font
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                    }
                    Column {
                        x: 54
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 54 - 70
                        Text {
                            width: parent.width
                            text: songRow.modelData.title
                            elide: Text.ElideRight
                            color: Glass.ink
                            font.family: Glass.font
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Text {
                            width: parent.width
                            text: songRow.modelData.artist
                            elide: Text.ElideRight
                            color: Glass.mute
                            font.family: Glass.font
                            font.pixelSize: 11
                        }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: songRow.modelData.time
                        color: Glass.faint
                        font.family: Glass.font
                        font.pixelSize: 11
                    }
                }
            }
            SmallLabel { visible: wrapped.stats.hasData && wrapped.artists.length > 0; leftPadding: 6; text: qsTr("Top artists") }
            Flow {
                visible: wrapped.stats.hasData
                width: parent.width
                spacing: 6
                Repeater {
                    model: wrapped.stats.hasData ? wrapped.artists.slice(0, 3) : []
                    Chip {
                        required property var modelData
                        height: 28
                        pad: 12
                        label: modelData.name
                    }
                }
            }
            Chip {
                visible: wrapped.stats.hasData
                height: 26
                pad: 12
                dimmed: true
                label: qsTr("Reset my stats")
                onClicked: backend.forgetStats()
            }
            Item { width: 1; height: 4 }
        }
    }
}
