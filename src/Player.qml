import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// The whole player screen: artwork-first now playing, with the library
// sliding over it. Everything it needs comes from `backend`.
Item {
    id: root

    property var window: null
    property bool soundOpen: false
    readonly property bool nowView: backend.kind === "now"
    readonly property bool searching: backend.kind === "search"
    readonly property alias searchText: search.text
    readonly property bool searchVisible: searching
    readonly property bool hasTrack: backend.track !== ""

    readonly property color ink: "#fff6ee"
    readonly property color mute: "#a6f8e7dc"
    readonly property color faint: "#5cf8e7dc"
    readonly property color glass: "#1ff8e7dc"
    readonly property color hair: "#2bfff6ee"
    readonly property color accent: theme.accent

    function clock(seconds) {
        const whole = Math.max(0, Math.round(seconds))
        const m = Math.floor(whole / 60)
        const s = whole % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    // One path-drawn glyph, so no icon font is needed.
    component Glyph: Shape {
        id: glyph
        property string path: ""
        property color color: root.ink
        property real size: 24
        width: 24
        height: 24
        scale: size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: glyph.color
            strokeColor: "transparent"
            PathSvg { path: glyph.path }
        }
    }

    component RoundButton: Item {
        id: button
        property string path: ""
        property real size: 22
        property real hit: 44
        property color tint: root.ink
        property bool filled: false
        property color fill: root.ink
        signal clicked()
        width: hit
        height: hit
        scale: tap.pressed ? 0.88 : (tap.containsMouse ? 1.06 : 1)
        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: button.filled ? button.fill : root.glass
            opacity: button.filled ? 1 : (tap.containsMouse || tap.pressed ? 1 : 0)
            Behavior on opacity { NumberAnimation { duration: 160 } }
        }
        Glyph {
            x: (button.width - button.size) / 2
            y: (button.height - button.size) / 2
            path: button.path
            size: button.size
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


    component FxSlider: Item {
        id: fx
        property string label: ""
        property real value: 0
        property real from: -1
        property real to: 1
        property string unit: ""
        signal moved(real v)
        width: parent ? parent.width : 0
        height: 52
        readonly property real span: to - from
        readonly property real ratio: (value - from) / span
        readonly property real zero: (0 - from) / span
        Text {
            text: fx.label
            color: root.ink
            font.family: "Inter"
            font.pixelSize: 14
            font.weight: Font.Medium
        }
        Text {
            anchors.right: parent.right
            text: fx.unit
            color: root.mute
            font.family: "Inter"
            font.pixelSize: 12
            font.features: { "tnum": 1 }
        }
        Rectangle {
            id: track
            y: 30
            width: parent.width
            height: 4
            radius: 2
            color: "#33ffffff"
            Rectangle {
                x: parent.width * Math.min(fx.ratio, fx.zero)
                width: parent.width * Math.abs(fx.ratio - fx.zero)
                height: parent.height
                radius: 2
                color: root.ink
                Behavior on width { enabled: !drag.pressed; NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }
            // centre mark for bass and treble
            Rectangle {
                visible: fx.from < 0
                x: parent.width * fx.zero - 1
                y: -4
                width: 2
                height: 12
                radius: 1
                color: "#59ffffff"
            }
            Rectangle {
                x: parent.width * fx.ratio - width / 2
                y: (parent.height - height) / 2
                width: 16
                height: 16
                radius: 8
                color: root.ink
                scale: drag.pressed ? 1.25 : 1
                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }
            }
        }
        MouseArea {
            id: drag
            y: 20
            width: parent.width
            height: 32
            cursorShape: Qt.PointingHandCursor
            function apply(mx) {
                let r = Math.max(0, Math.min(1, mx / width))
                let v = fx.from + r * fx.span
                // snap to the centre so "flat" is easy to hit
                if (fx.from < 0 && Math.abs(v) < 0.04 * fx.span) v = 0
                fx.moved(v)
            }
            onPressed: function (mouse) { apply(mouse.x) }
            onPositionChanged: function (mouse) { if (pressed) apply(mouse.x) }
        }
    }

    // ---- backdrop: the cover, blurred and dimmed, so every song has its own colour ----
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.darker(root.accent, 3.2) }
            GradientStop { position: 1; color: "#100d0a" }
        }
    }
    Image {
        id: backdrop
        anchors.fill: parent
        source: backend.artwork
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 600; easing.type: Easing.InOutQuad } }
        layer.enabled: true
        layer.effect: MultiEffect {
            blurEnabled: true
            blurMax: 64
            blur: 1.0
            saturation: 0.25
            brightness: -0.42
            autoPaddingEnabled: false
        }
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#33000000" }
            GradientStop { position: 0.55; color: "#59000000" }
            GradientStop { position: 1; color: "#b3000000" }
        }
    }

    // Drag the frameless window by any empty background.
    MouseArea {
        anchors.fill: parent
        z: -1
        onPressed: if (root.window) root.window.startSystemMove()
    }

    // ================= NOW PLAYING =================
    Item {
        id: nowPane
        anchors.fill: parent
        opacity: root.nowView ? 1 : 0
        scale: root.nowView ? 1 : 0.965
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }

        readonly property real pad: 24
        readonly property real artSize: Math.min(width - pad * 2, height - 300)

        // top bar
        RoundButton {
            x: 12
            y: 12
            path: "M4 6h16v2H4zM4 11h16v2H4zM4 16h10v2H4z"
            size: 20
            hit: 40
            onClicked: backend.menu()
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 22
            text: qsTr("Now Playing")
            color: root.faint
            font.family: "Inter"
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 1.4
            font.capitalization: Font.AllUppercase
        }
        RoundButton {
            x: 52
            y: 12
            path: "M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"
            size: 20
            hit: 40
            onClicked: backend.openSearch()
        }

        // artwork
        Item {
            id: artFrame
            width: nowPane.artSize
            height: nowPane.artSize
            x: (parent.width - width) / 2
            y: 64
            scale: backend.playing ? 1 : 0.9
            Behavior on scale { NumberAnimation { duration: 520; easing.type: Easing.OutBack; easing.overshoot: 1.2 } }

            Item {
                id: artShadowSource
                anchors.fill: parent
                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    color: "black"
                }
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "#000000"
                    shadowOpacity: 0.55
                    shadowBlur: 1.0
                    shadowVerticalOffset: 14
                    shadowScale: 0.96
                    autoPaddingEnabled: true
                }
            }
            Rectangle {
                id: artFallback
                anchors.fill: parent
                radius: 22
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(root.accent, 1.15) }
                    GradientStop { position: 1; color: Qt.darker(root.accent, 2.4) }
                }
                Glyph {
                    x: (parent.width - 64) / 2
                    y: (parent.height - 64) / 2
                    size: 64
                    color: "#66ffffff"
                    path: "M12 3v10.55A4 4 0 1 0 14 17V7h4V3h-6z"
                }
            }
            Image {
                id: cover
                anchors.fill: parent
                source: backend.artwork
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 450; easing.type: Easing.InOutQuad } }
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: coverMask
                }
            }
            Rectangle {
                id: coverMask
                anchors.fill: parent
                radius: 22
                visible: false
                layer.enabled: true
            }
            Rectangle {
                anchors.fill: parent
                radius: 22
                color: "transparent"
                border.color: root.hair
                border.width: 1
            }
        }

        // title block: the song once, the artist once
        Column {
            id: meta
            x: nowPane.pad
            y: artFrame.y + artFrame.height + 26
            width: parent.width - nowPane.pad * 2
            spacing: 4
            Text {
                width: parent.width
                text: root.hasTrack ? backend.track : (backend.playing ? qsTr("Loading…") : qsTr("Nothing playing"))
                color: root.ink
                elide: Text.ElideRight
                font.family: "Inter"
                font.pixelSize: 21
                font.weight: Font.Bold
            }
            Text {
                width: parent.width
                text: backend.artist + (backend.album !== "" ? "  ·  " + backend.album : "")
                color: root.mute
                elide: Text.ElideRight
                font.family: "Inter"
                font.pixelSize: 13
                visible: text !== ""
            }
        }

        // progress
        Item {
            id: scrub
            x: nowPane.pad
            y: meta.y + 62
            width: parent.width - nowPane.pad * 2
            height: 34
            property bool dragging: false
            property real dragRatio: 0
            readonly property real ratio: dragging ? dragRatio
                : (backend.duration > 0 ? Math.min(1, backend.position / backend.duration) : 0)

            Rectangle {
                id: rail
                width: parent.width
                height: scrubArea.containsMouse || scrub.dragging ? 6 : 4
                y: 8
                radius: height / 2
                color: "#33ffffff"
                Behavior on height { NumberAnimation { duration: 140 } }
                Rectangle {
                    width: parent.width * scrub.ratio
                    height: parent.height
                    radius: height / 2
                    color: root.ink
                    Behavior on width {
                        enabled: !scrub.dragging
                        NumberAnimation { duration: 120; easing.type: Easing.Linear }
                    }
                }
                Rectangle {
                    x: parent.width * scrub.ratio - width / 2
                    y: (parent.height - height) / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: root.ink
                    scale: scrubArea.containsMouse || scrub.dragging ? 1 : 0
                    Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutBack } }
                }
            }
            Text {
                y: 20
                text: scrub.dragging ? root.clock(scrub.dragRatio * backend.duration) : backend.positionLabel
                color: root.faint
                font.family: "Inter"
                font.pixelSize: 11
                font.features: { "tnum": 1 }
            }
            Text {
                y: 20
                anchors.right: parent.right
                text: "-" + root.clock(Math.max(0, backend.duration - (scrub.dragging ? scrub.dragRatio * backend.duration : backend.position)))
                color: root.faint
                font.family: "Inter"
                font.pixelSize: 11
                font.features: { "tnum": 1 }
            }
            MouseArea {
                id: scrubArea
                anchors.fill: parent
                anchors.topMargin: -6
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                function ratioAt(x) { return Math.max(0, Math.min(1, x / width)) }
                onPressed: function (mouse) {
                    scrub.dragRatio = ratioAt(mouse.x)
                    scrub.dragging = true
                }
                onPositionChanged: function (mouse) { if (pressed) scrub.dragRatio = ratioAt(mouse.x) }
                onReleased: {
                    if (backend.duration > 0)
                        backend.seek(backend.duration * scrub.dragRatio)
                    scrub.dragging = false
                }
                onCanceled: scrub.dragging = false
            }
        }

        // transport
        Row {
            id: transport
            anchors.horizontalCenter: parent.horizontalCenter
            y: scrub.y + 46
            spacing: 22
            RoundButton {
                anchors.verticalCenter: parent.verticalCenter
                path: "M6 6h2v12H6zM9.5 12l8.5 6V6z"
                size: 26
                hit: 52
                onClicked: backend.previousTrack()
            }
            RoundButton {
                anchors.verticalCenter: parent.verticalCenter
                path: backend.playing ? "M6 5h4v14H6zM14 5h4v14h-4z" : "M8 5v14l11-7z"
                size: 30
                hit: 68
                filled: true
                fill: root.ink
                tint: "#14110c"
                onClicked: backend.playPause()
            }
            RoundButton {
                anchors.verticalCenter: parent.verticalCenter
                path: "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"
                size: 26
                hit: 52
                onClicked: backend.nextTrack()
            }
        }

        // volume
        Item {
            id: volume
            x: nowPane.pad + 6
            y: parent.height - 44
            width: parent.width - (nowPane.pad + 6) * 2 - 100
            height: 28
            Glyph {
                y: 5
                size: 18
                color: root.faint
                path: "M3 9v6h4l5 5V4L7 9H3z"
            }
            Rectangle {
                id: vrail
                x: 28
                width: parent.width - 28
                height: 4
                y: 12
                radius: 2
                color: "#33ffffff"
                Rectangle {
                    width: parent.width * backend.volume
                    height: parent.height
                    radius: 2
                    color: root.mute
                    Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                }
            }
            MouseArea {
                x: 28
                width: parent.width - 28
                height: parent.height
                cursorShape: Qt.PointingHandCursor
                function apply(mx) { backend.setVolume(Math.max(0, Math.min(1, mx / width))) }
                onPressed: function (mouse) { apply(mouse.x) }
                onPositionChanged: function (mouse) { if (pressed) apply(mouse.x) }
            }
        }

        RoundButton {
            x: parent.width - width - 62
            y: parent.height - 52
            hit: 44
            size: 22
            tint: effects.enabled ? root.accent : root.ink
            path: "M3 17v2h6v-2H3zM3 5v2h10V5H3zm10 14v2h8v-2h-8zM7 9v2H3v2h4v2h2V9H7zm14 4v-2H11v2h10zm-6-4h2V7h4V5h-4V3h-2v6z"
            onClicked: root.soundOpen = true
        }
        RoundButton {
            x: parent.width - width - 14
            y: parent.height - 52
            hit: 44
            size: 22
            path: "M3 13h2v-2H3v2zm0 4h2v-2H3v2zm0-8h2V7H3v2zm4 4h14v-2H7v2zm0 4h14v-2H7v2zM7 7v2h14V7H7z"
            onClicked: backend.openQueue()
        }
    }

    // ================= LIBRARY =================
    Item {
        id: libraryPane
        width: parent.width
        height: parent.height
        x: root.nowView ? width * 0.35 : 0
        opacity: root.nowView ? 0 : 1
        visible: opacity > 0.01
        Behavior on x { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
        Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

        RoundButton {
            id: back
            x: 12
            y: 12
            path: "M15.41 7.41L14 6l-6 6 6 6 1.41-1.41L10.83 12z"
            size: 24
            hit: 40
            onClicked: backend.back()
        }
        Text {
            anchors.left: back.right
            anchors.right: parent.right
            anchors.rightMargin: 84
            y: 12
            height: 40
            verticalAlignment: Text.AlignVCenter
            text: backend.title
            color: root.ink
            elide: Text.ElideRight
            font.family: "Inter"
            font.pixelSize: 20
            font.weight: Font.Bold
        }

        // search field, only on the search screen
        Item {
            id: searchBox
            x: 16
            y: 60
            width: parent.width - 32
            height: root.searching ? 40 : 0
            visible: height > 1
            clip: true
            Behavior on height { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            onVisibleChanged: if (visible) {
                search.text = ""
                search.forceActiveFocus()
            }
            Rectangle {
                anchors.fill: parent
                radius: 14
                color: root.glass
                border.color: search.activeFocus ? "#80fff6ee" : root.hair
                border.width: 1
            }
            Glyph {
                x: 12
                y: 11
                size: 18
                color: root.faint
                path: "M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"
            }
            Text {
                x: 40
                anchors.verticalCenter: parent.verticalCenter
                visible: search.text.length === 0
                text: qsTr("Artists, songs, albums")
                color: root.faint
                font.family: "Inter"
                font.pixelSize: 14
            }
            TextInput {
                id: search
                x: 40
                width: parent.width - 52
                height: parent.height
                verticalAlignment: TextInput.AlignVCenter
                color: root.ink
                font.family: "Inter"
                font.pixelSize: 14
                clip: true
                selectByMouse: true
                onTextChanged: backend.setSearch(text)
            }
        }

        ListView {
            id: list
            x: 8
            y: searchBox.y + searchBox.height + (root.searching ? 8 : 0)
            width: parent.width - 16
            height: parent.height - y - (miniBar.visible ? miniBar.height + 24 : 12)
            clip: true
            model: backend.rows
            currentIndex: backend.selected
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: true
            highlightMoveDuration: 180
            highlightMoveVelocity: -1
            preferredHighlightBegin: height * 0.25
            preferredHighlightEnd: height * 0.75
            highlightRangeMode: ListView.ApplyRange
            onCountChanged: positionViewAtBeginning()

            WheelHandler {
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function (event) { backend.wheel(event.angleDelta.y < 0 ? 1 : -1) }
            }

            highlight: Rectangle {
                radius: 14
                color: "#26fff6ee"
                border.color: "#33fff6ee"
                border.width: 1
            }

            delegate: Item {
                id: row
                required property int index
                required property var modelData
                width: list.width
                height: 54
                readonly property bool current: index === backend.selected
                readonly property bool queueable: modelData.action === "play"
                HoverHandler { id: hover }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: row.queueable ? 84 : 40
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1
                    Text {
                        width: parent.width
                        text: row.modelData.label
                        color: root.ink
                        elide: Text.ElideRight
                        font.family: "Inter"
                        font.pixelSize: 15
                        font.weight: row.current ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        width: parent.width
                        visible: text !== ""
                        text: row.modelData.detail
                        color: root.mute
                        elide: Text.ElideRight
                        font.family: "Inter"
                        font.pixelSize: 12
                    }
                }
                Glyph {
                    id: arrow
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    y: (parent.height - 18) / 2
                    size: 18
                    color: root.faint
                    visible: !row.queueable
                    opacity: row.current ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 160 } }
                    path: "M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6z"
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        backend.wheel(row.index - backend.selected)
                        backend.center()
                    }
                }
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    visible: row.queueable
                    opacity: hover.hovered || row.current ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                    RoundButton {
                        path: "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"
                        size: 18
                        hit: 38
                        onClicked: backend.addToQueue(row.index, true)
                    }
                    RoundButton {
                        path: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"
                        size: 20
                        hit: 38
                        onClicked: backend.addToQueue(row.index, false)
                    }
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: list.y + 40
            visible: list.count === 0
            text: backend.status !== "" ? backend.status : qsTr("Nothing here")
            color: root.faint
            font.family: "Inter"
            font.pixelSize: 14
        }

        // compact now-playing bar so music stays one tap away
        Rectangle {
            id: miniBar
            visible: root.hasTrack && !root.searching
            x: 12
            width: parent.width - 24
            height: 60
            y: parent.height - height - 12
            radius: 18
            color: "#33140f0a"
            border.color: root.hair
            border.width: 1
            Item {
                id: thumbClip
                x: 8
                y: 8
                width: 44
                height: 44
                Rectangle {
                    anchors.fill: parent
                    radius: 11
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.lighter(root.accent, 1.15) }
                        GradientStop { position: 1; color: Qt.darker(root.accent, 2.4) }
                    }
                }
                Image {
                    anchors.fill: parent
                    source: backend.artwork
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    layer.enabled: true
                    layer.effect: MultiEffect { maskEnabled: true; maskSource: thumbMask }
                }
                Rectangle { id: thumbMask; anchors.fill: parent; radius: 11; visible: false; layer.enabled: true }
            }
            Column {
                anchors.left: thumbClip.right
                anchors.leftMargin: 12
                anchors.right: mini.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    width: parent.width
                    text: backend.track
                    color: root.ink
                    elide: Text.ElideRight
                    font.family: "Inter"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: backend.artist
                    color: root.mute
                    elide: Text.ElideRight
                    font.family: "Inter"
                    font.pixelSize: 12
                }
            }
            RoundButton {
                id: mini
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                path: backend.playing ? "M6 5h4v14H6zM14 5h4v14h-4z" : "M8 5v14l11-7z"
                size: 22
                hit: 44
                onClicked: backend.playPause()
            }
            MouseArea {
                anchors.left: parent.left
                anchors.right: mini.left
                height: parent.height
                cursorShape: Qt.PointingHandCursor
                onClicked: backend.openPlayer()
            }
        }
    }

    // ================= SOUND =================
    Rectangle {
        id: scrim
        anchors.fill: parent
        z: 8
        color: "#99000000"
        opacity: root.soundOpen ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 240 } }
        MouseArea { anchors.fill: parent; onClicked: root.soundOpen = false }
    }
    Rectangle {
        id: sheet
        z: 9
        width: parent.width - 16
        x: 8
        height: 340
        y: root.soundOpen ? parent.height - height - 8 : parent.height + 8
        radius: 26
        color: "#f21a1410"
        border.color: root.hair
        border.width: 1
        Behavior on y { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }
        MouseArea { anchors.fill: parent }   // keep taps inside the sheet

        Item {
            id: sheetHead
            x: 22
            y: 18
            width: parent.width - 44
            height: 34
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Sound")
                color: root.ink
                font.family: "Inter"
                font.pixelSize: 20
                font.weight: Font.Bold
            }
            // on/off switch
            Rectangle {
                id: sw
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 52
                height: 30
                radius: 15
                color: effects.enabled ? root.accent : "#40ffffff"
                opacity: effects.available ? 1 : 0.4
                Behavior on color { ColorAnimation { duration: 200 } }
                Rectangle {
                    y: 3
                    x: effects.enabled ? parent.width - width - 3 : 3
                    width: 24
                    height: 24
                    radius: 12
                    color: "#fff6ee"
                    Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: effects.available
                    cursorShape: Qt.PointingHandCursor
                    onClicked: effects.enabled = !effects.enabled
                }
            }
        }
        Text {
            id: sheetNote
            x: 22
            y: sheetHead.y + sheetHead.height + 2
            width: parent.width - 44
            text: !effects.available ? qsTr("Needs PipeWire (pipewire, pw-cli, pactl).")
                : effects.enabled && !effects.active ? qsTr("Starting…")
                : effects.enabled ? qsTr("Shaping Click's audio only.")
                : qsTr("Off. Move a slider to turn it on.")
            color: root.faint
            font.family: "Inter"
            font.pixelSize: 12
            elide: Text.ElideRight
        }
        Column {
            x: 22
            y: sheetNote.y + 30
            width: parent.width - 44
            spacing: 6
            FxSlider {
                label: qsTr("Bass")
                value: effects.bass
                unit: (effects.bass >= 0 ? "+" : "") + (effects.bass * 10).toFixed(1) + " dB"
                onMoved: function (v) { effects.bass = v; if (!effects.enabled) effects.enabled = true }
            }
            FxSlider {
                label: qsTr("Treble")
                value: effects.treble
                unit: (effects.treble >= 0 ? "+" : "") + (effects.treble * 10).toFixed(1) + " dB"
                onMoved: function (v) { effects.treble = v; if (!effects.enabled) effects.enabled = true }
            }
            FxSlider {
                label: qsTr("Reverb")
                from: 0
                to: 1
                value: effects.reverb
                unit: Math.round(effects.reverb * 100) + "%"
                onMoved: function (v) { effects.reverb = v; if (!effects.enabled) effects.enabled = true }
            }
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height - height - 18
            width: 110
            height: 36
            radius: 18
            color: root.glass
            border.color: root.hair
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: qsTr("Reset")
                color: root.ink
                font.family: "Inter"
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: effects.reset()
            }
        }
    }

    // window controls for the frameless window, above both panes
    Row {
        id: chrome
        z: 11
        anchors.right: parent.right
        anchors.rightMargin: 10
        y: 16
        spacing: 2
        RoundButton {
            path: "M5 11h14v2H5z"
            size: 16
            hit: 32
            tint: root.mute
            onClicked: if (root.window) root.window.showMinimized()
        }
        RoundButton {
            path: "M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"
            size: 16
            hit: 32
            tint: root.mute
            onClicked: {
                if (root.window && root.window.rememberPosition) root.window.rememberPosition()
                Qt.quit()
            }
        }
    }

    // brief confirmation, e.g. "Added to Up Next"
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - 120
        z: 10
        height: 38
        width: toastText.implicitWidth + 36
        radius: height / 2
        color: "#e6fff6ee"
        opacity: backend.toast !== "" ? 1 : 0
        visible: opacity > 0.01
        scale: backend.toast !== "" ? 1 : 0.9
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on scale { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }
        property string last: ""
        Connections { target: backend; function onToastChanged() { if (backend.toast !== "") toast.last = backend.toast } }
        Text {
            id: toastText
            anchors.centerIn: parent
            text: toast.last
            color: "#14110c"
            font.family: "Inter"
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }
}
