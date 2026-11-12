import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

// Cinema: the whole screen for the song that is playing.
//
//   backdrop   the cover's colours (never the picture): the flowing mesh, with soft pools of liquid light on top
//              that swell with the spectrum and brighten on each kick (LiquidField, shaders/liquid.frag)
//   left       the cover, big, breathing with the bass the way the mini player does, with a halo of its colour,
//              a ring on every kick, a soft shadow, and a chromatic fringe at its rim; under it a liquid-glass
//              transport card of the same width with the same liquid moving inside it
//   right      the words, large, synced to the song; the word being sung lifts (LyricsView)
//
// The window (MainWindow.qml) goes borderless full screen around it; Escape or F11 leaves.
Item {
    id: cinema

    property bool active: false
    signal exitRequested()
    // The same controls as the mini player. Search, + , Sound and Settings open as menus over the picture,
    // anchored to the controls, with the moving backdrop frosted behind them.
    signal heartRequested()
    signal toggleLyrics()
    property Item player: null              // for the settings it owns (the animation style)
    property string menu: ""                // "", "search", "plus", "sound" or "settings"
    readonly property bool typing: menu === "search" || plusPop.typing
    function toggleMenu(which) { if (menu === which) closeMenu(); else menu = which }
    function closeMenu() { menu = "" }

    // With the words off, the cover and the moving background have the whole screen.
    property bool lyricsOn: true
    // 0 is the words layout, 1 the cover layout. On a bouncy spring, so the cover swells past its size and settles.
    property real lf: lyricsOn ? 0 : 1
    Behavior on lf { SpringAnimation { spring: 3.4; damping: 0.26; epsilon: 0.0008 } }
    // A flare of light that runs through everything for a moment when the layout changes.
    property real bloom: 0
    SequentialAnimation {
        id: bloomAnim
        NumberAnimation { target: cinema; property: "bloom"; from: 0; to: 1; duration: 260; easing.type: Easing.OutCubic }
        NumberAnimation { target: cinema; property: "bloom"; to: 0; duration: 1100; easing.type: Easing.OutQuad }
    }
    onLyricsOnChanged: {
        if (!active) return
        bloomAnim.restart()
        for (let i = 0; i < 3; ++i) burst.itemAt(i).fire(1.4, i * 140)
    }

    // ---- the two layouts: where the cover and the song block sit (all in screen pixels) ----
    function mixv(a, b, t) { return a + (b - a) * t }
    readonly property real dockHeight: 188
    readonly property real gap: 28          // cover to title
    readonly property real titleHeight: 72  // title and artist
    readonly property real gap2: 20         // title to the controls
    readonly property real below: gap + titleHeight + gap2 + dockHeight
    // words layout: the cover on the left, the song and controls under it
    readonly property real cb0w: width * 0.43
    readonly property real side0: Math.max(120, Math.min(cb0w * 0.8, height - 110 - below))
    readonly property real art0x: width * 0.05 + (cb0w - side0) / 2
    readonly property real art0y: (height - (side0 + below)) / 2
    // cover layout: one centred column, the cover over the song and the controls. When the pointer rests the
    // controls slip away and the cover grows into the room they leave (`rest`).
    readonly property real side1c: Math.max(160, Math.min(width * 0.6, height - below - 72))
    readonly property real sideBig: Math.max(160, Math.min(width * 0.7, height * 0.86))
    property real rest: lf > 0.5 && !dockShown && menu === "" ? 1 : 0
    Behavior on rest { SpringAnimation { spring: 2.2; damping: 0.3; epsilon: 0.001 } }
    readonly property real side1: mixv(side1c, sideBig, rest)
    readonly property real art1x: (width - side1) / 2
    readonly property real art1yCol: (height - (side1c + below)) / 2
    readonly property real art1y: mixv(art1yCol, (height - sideBig) / 2, rest)
    readonly property real info1w: Math.max(440, Math.min(620, side1c))
    readonly property real info1x: (width - info1w) / 2
    readonly property real info1y: art1yCol + side1c + gap

    anchors.fill: parent
    opacity: active ? 1 : 0
    visible: opacity > 0.01
    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }

    // (The words are fetched while this or the lyrics sheet is open: one Binding in Player.qml owns wantLyrics.)

    // ---- the dock hides when the pointer rests ----
    property bool dockShown: true
    function poke() {
        dockShown = true
        idle.restart()
    }
    Timer { id: idle; interval: 3000; running: cinema.active; onTriggered: cinema.dockShown = false }
    onActiveChanged: { if (active) poke(); else menu = "" }

    HoverHandler {
        id: hover
        cursorShape: cinema.dockShown ? Qt.ArrowCursor : Qt.BlankCursor
        // The pointer only wakes the title and the leave button; the cover follows the music, not the mouse.
        onPointChanged: cinema.poke()
    }

    // ---- backdrop ----
    Item {
        id: backdrop
        anchors.fill: parent
        clip: true
        // Oversized and turning, so its corners never show: one slow turn takes four minutes.
        Item {
            id: turntable
            anchors.centerIn: parent
            width: Math.hypot(cinema.width, cinema.height) * 1.05
            height: width
            RotationAnimation on rotation {
                from: 0
                to: 360
                duration: 240000
                loops: Animation.Infinite
                running: cinema.active && backend.playing
            }
            MeshBackground {
                anchors.fill: parent
                running: cinema.active && backend.playing
                calm: 0
            }
        }
        // Waves and dots in the same colours, alive with the sound. Not turning: the music sets how they move.
        LiquidField {
            anchors.fill: parent
            running: cinema.active
            opacity: 1 + 0.3 * cinema.bloom
        }
        // With the words off the liquid has the whole screen: a second, faster layer joins it.
        LiquidField {
            anchors.fill: parent
            running: cinema.active && cinema.lf > 0.02
            pace: 1.4
            opacity: Math.max(0, Math.min(1, cinema.lf)) * 0.75
            visible: opacity > 0.01
        }
        // Darker towards the words, so they stay easy to read over any cover.
        Rectangle {
            anchors.fill: parent
            opacity: 1 - 0.85 * Math.max(0, Math.min(1, cinema.lf))
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#14000000" }
                GradientStop { position: 0.55; color: "#2e000000" }
                GradientStop { position: 1.0; color: "#59000000" }
            }
        }
    }

    // The flare when the layout changes.
    Rectangle {
        anchors.fill: parent
        color: Glass.accentLight
        opacity: 0.16 * cinema.bloom
        visible: opacity > 0.005
    }

    // ---- the cover, with the song and the controls ----
    Item {
        id: coverBox
        x: 0
        y: 0
        width: cinema.width
        height: cinema.height

        // The cover breathes with the bass through a spring, as in the player and the mini player, a little
        // more here because there is room for it: up to 6 %.
        property real pump: Glass.reactive ? (0.06 + 0.03 * cinema.lf) * Glass.cover : 0
        Behavior on pump { SpringAnimation { spring: 9; damping: 0.38; epsilon: 0.0005 } }
        readonly property real glowLevel: !backend.playing ? 0.15 : Glass.reactive ? Math.min(1, 0.25 + 0.75 * Glass.cover) : 0.35

        Item {
            id: art
            x: cinema.mixv(cinema.art0x, cinema.art1x, cinema.lf)
            y: cinema.mixv(cinema.art0y, cinema.art1y, cinema.lf)
            width: cinema.mixv(cinema.side0, cinema.side1, cinema.lf)
            height: width
            scale: backend.playing ? 1 : 0.94
            Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.2; epsilon: 0.001 } }
            transform: Scale {
                origin.x: art.width / 2
                origin.y: art.height / 2
                xScale: 1 + coverBox.pump
                yScale: 1 + coverBox.pump
            }
            readonly property real corner: width * 0.05

            // A halo of the cover's colour behind it that flares with the bass.
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.92
                height: parent.height * 0.92
                radius: art.corner * 2
                color: Glass.accent
                opacity: Math.min(1, coverBox.glowLevel * (0.85 + 0.15 * cinema.lf) + 0.5 * cinema.bloom)
                scale: 1.04 + (Glass.reactive ? (0.16 + 0.1 * cinema.lf) * Glass.cover : 0.03) + 0.18 * cinema.bloom + 0.06 * cinema.lf
                layer.enabled: true
                layer.effect: MultiEffect { blurEnabled: true; blur: 1.0; blurMax: 64; autoPaddingEnabled: true }
            }
            // The second colour, wider and slower, only when the cover has the room.
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 1.1
                height: parent.height * 1.1
                radius: art.corner * 2.4
                color: Glass.secondary
                opacity: cinema.lf * 0.5 * coverBox.glowLevel
                scale: 1.08 + (Glass.reactive ? 0.2 * Glass.cover : 0.04) + 0.25 * cinema.bloom
                visible: opacity > 0.01
                layer.enabled: true
                layer.effect: MultiEffect { blurEnabled: true; blur: 1.0; blurMax: 64; autoPaddingEnabled: true }
            }
            // One soft shadow, close under the cover and well blurred, so it floats a little above the light.
            Rectangle {
                anchors.fill: parent
                radius: art.corner
                color: "black"
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: "black"
                    shadowOpacity: 0.42
                    shadowBlur: 1.0
                    blurMax: 64
                    shadowVerticalOffset: 14
                    shadowScale: 0.97
                    autoPaddingEnabled: true
                }
            }

            // A ring leaves the cover on every clear hit, one after the other.
            Repeater {
                id: rings
                property int next: 0
                model: 3
                Rectangle {
                    id: ring
                    required property int index
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: art.corner * 1.4
                    color: "transparent"
                    border.color: Glass.accentLight
                    border.width: 2
                    opacity: 0
                    function fire(strength) {
                        ringAnim.strength = strength
                        ringAnim.restart()
                    }
                    ParallelAnimation {
                        id: ringAnim
                        property real strength: 1
                        NumberAnimation { target: ring; property: "scale"; from: 1.0; to: 1.06 + 0.06 * ringAnim.strength; duration: 700; easing.type: Easing.OutCubic }
                        NumberAnimation { target: ring; property: "opacity"; from: 0.35 * ringAnim.strength; to: 0; duration: 700; easing.type: Easing.OutQuad }
                    }
                }
            }
            // The burst when the layout changes: three rings leave the cover, one after the other.
            Repeater {
                id: burst
                model: 3
                Rectangle {
                    id: bring
                    required property int index
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: art.corner * 1.4
                    color: "transparent"
                    border.color: Glass.accentLight
                    border.width: 3
                    opacity: 0
                    function fire(strength, delay) { bringAnim.strength = strength; bringDelay.interval = delay; bringDelay.restart() }
                    Timer { id: bringDelay; onTriggered: bringAnim.restart() }
                    ParallelAnimation {
                        id: bringAnim
                        property real strength: 1
                        NumberAnimation { target: bring; property: "scale"; from: 1.0; to: 1.18 + 0.1 * bring.index; duration: 1000; easing.type: Easing.OutCubic }
                        NumberAnimation { target: bring; property: "opacity"; from: 0.55; to: 0; duration: 1000; easing.type: Easing.OutQuad }
                    }
                }
            }
            Connections {
                target: audio
                enabled: cinema.active
                function onCoverHit(strength) {
                    rings.itemAt(rings.next % 3).fire(strength)
                    rings.next += 1
                }
            }

            Item {
                id: pictureBox
                anchors.fill: parent
                // Rounded corners, then the fringe on top of the rounded picture.
                Rectangle { id: roundMask; anchors.fill: parent; radius: art.corner; visible: false; layer.enabled: true }
                Item {
                    id: pic
                    anchors.fill: parent
                    layer.enabled: true
                    layer.effect: MultiEffect { maskEnabled: true; maskSource: roundMask }
                    // Until the picture arrives (or when there is none), the cover's own colours stand in for it.
                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0; color: Glass.accentLight }
                            GradientStop { position: 1; color: Glass.secondary }
                        }
                    }
                    Image {
                        anchors.fill: parent
                        source: backend.artwork
                        sourceSize: Qt.size(1024, 1024)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                    }
                }
                // The fringe samples the picture through a ShaderEffectSource, which does not carry the picture's
                // rounded mask, so the result is rounded again here (this is why the corners were square).
                Item {
                    anchors.fill: parent
                    layer.enabled: true
                    layer.effect: MultiEffect { maskEnabled: true; maskSource: roundMask }
                    ShaderEffect {
                        anchors.fill: parent
                        property var source: ShaderEffectSource { sourceItem: pic; hideSource: true; live: true }
                        property real amount: 0.005
                        property real pump: Glass.reactive ? Math.min(1, Glass.cover) : 0
                        fragmentShader: Qt.resolvedUrl("shaders/fringe.frag.qsb")
                    }
                }
                // A thin lit rim and a faint gloss at the top, like a print under glass.
                Rectangle {
                    anchors.fill: parent
                    radius: art.corner
                    color: "transparent"
                    border.width: 1
                    border.color: "#40ffffff"
                }
                Rectangle {
                    anchors.fill: parent
                    radius: art.corner
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1affffff" }
                        GradientStop { position: 0.35; color: "transparent" }
                    }
                }
            }
        }

        // ---- the song, under the cover ----
        Item {
            id: titleBlock
            x: cinema.mixv(cinema.art0x, cinema.info1x, cinema.lf)
            y: cinema.mixv(cinema.art0y + cinema.side0 + cinema.gap, cinema.info1y, cinema.lf)
            width: cinema.mixv(cinema.side0, cinema.info1w, cinema.lf)
            height: cinema.titleHeight
            opacity: 1 - cinema.rest
            transform: Translate { y: 40 * cinema.rest }
            clip: false
            // What is on show; it changes half way through the swap, while the old words are faded out.
            property string shownTitle: backend.track
            property string shownArtist: backend.artist
            property real shift: 0
            property real fade: 1
            readonly property string key: backend.track + "\u0000" + backend.artist
            onKeyChanged: swap.restart()
            SequentialAnimation {
                id: swap
                // The old song slides down 8 px and fades...
                ParallelAnimation {
                    NumberAnimation { target: titleBlock; property: "shift"; to: 8; duration: 160; easing.type: Easing.InCubic }
                    NumberAnimation { target: titleBlock; property: "fade"; to: 0; duration: 160; easing.type: Easing.InCubic }
                }
                ScriptAction { script: { titleBlock.shownTitle = backend.track; titleBlock.shownArtist = backend.artist; titleBlock.shift = -8 } }
                // ...and the new one comes in from 8 px above on a spring.
                ParallelAnimation {
                    SpringAnimation { target: titleBlock; property: "shift"; to: 0; spring: Glass.springPill; damping: Glass.dampingPill; epsilon: 0.05 }
                    NumberAnimation { target: titleBlock; property: "fade"; to: 1; duration: 260; easing.type: Easing.OutCubic }
                }
            }
            Column {
                width: parent.width
                y: titleBlock.shift
                opacity: titleBlock.fade
                spacing: 4
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: titleBlock.shownTitle
                    elide: Text.ElideRight
                    color: "white"
                    font.family: Glass.font
                    font.pixelSize: Math.round(28 + 8 * Math.max(0, Math.min(1, cinema.lf)))
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: titleBlock.shownArtist
                    elide: Text.ElideRight
                    color: Qt.rgba(1, 1, 1, 0.65)
                    font.family: Glass.font
                    font.pixelSize: Math.round(16 + 4 * Math.max(0, Math.min(1, cinema.lf)))
                    font.weight: Font.Medium
                }
            }
        }

        // ---- the controls: a card of liquid glass, the time bar on top and the transport under it ----
        Item {
            id: dockArea
            x: titleBlock.x
            y: titleBlock.y + titleBlock.height + cinema.gap2
            width: titleBlock.width
            height: cinema.dockHeight
            opacity: 1 - cinema.rest
            enabled: cinema.rest < 0.5
            transform: Translate { y: 60 * cinema.rest }
            scale: 1 + 0.012 * (Glass.reactive ? Glass.cover : 0)
            Behavior on scale { SpringAnimation { spring: 9; damping: 0.38; epsilon: 0.0005 } }

            // Liquid glass: the backdrop behind the card, blurred and brightened, with the liquid moving inside it.
            Item {
                id: dock
                anchors.fill: parent
                readonly property real radius: 36
                LiquidGlass {
                    anchors.fill: parent
                    radius: dock.radius
                    source: backdrop
                    backX: dockArea.x
                    backY: dockArea.y
                    live: cinema.active
                }

                // Top tier: elapsed, the waveform, time left.
                Item {
                    id: timeTier
                    x: 24
                    y: 16
                    width: parent.width - 48
                    height: 40
                    Text {
                        id: elapsed
                        anchors.verticalCenter: parent.verticalCenter
                        width: 46
                        text: backend.positionLabel
                        color: "white"
                        opacity: 0.85
                        font.family: Glass.font
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        font.features: { "tnum": 1 }
                    }
                    LiquidScrubber {
                        x: elapsed.width + 10
                        width: parent.width - 2 * (elapsed.width + 10)
                        height: 40
                        anchors.verticalCenter: parent.verticalCenter
                        compact: true
                        waveHeight: 34
                        position: backend.position
                        duration: backend.duration
                        playing: backend.playing
                        trackKey: backend.track + "\u0000" + backend.artist
                        style: "liquid"
                        onSeekRequested: function (seconds) { backend.seek(seconds) }
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 46
                        horizontalAlignment: Text.AlignRight
                        text: "-" + backend.formatClock(Math.max(0, backend.duration - backend.position))
                        color: "white"
                        opacity: 0.85
                        font.family: Glass.font
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        font.features: { "tnum": 1 }
                    }
                }

                // Bottom tier: shuffle, previous, play/pause, next, repeat, with room to breathe.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: timeTier.y + timeTier.height + 12
                    spacing: Math.max(10, Math.min(28, (dock.width - 340) / 4))
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        hit: 40; size: 20
                        path: "M10.59 9.17L5.41 4 4 5.41l5.17 5.17 1.42-1.41zM14.5 4l2.04 2.04L4 18.59 5.41 20 17.96 7.46 20 9.5V4h-5.5zm.33 9.41l-1.41 1.41 3.13 3.13L14.5 20H20v-5.5l-2.04 2.04-3.13-3.13z"
                        tint: backend.shuffle ? Glass.accentLight : Qt.rgba(1, 1, 1, 0.75)
                        marked: backend.shuffle
                        onClicked: backend.setShuffle(!backend.shuffle)
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        hit: 48; size: 26
                        tint: "white"
                        path: "M6 6h2v12H6zm3.5 6l8.5 6V6z"
                        onClicked: backend.previousTrack()
                    }
                    // 56 px, and it squeezes to 0.92 under the finger and springs back (IconButton's press).
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        hit: 56; size: 28
                        filled: true
                        fill: "white"
                        tint: Glass.inkText
                        playState: backend.playing ? 1 : 0
                        onClicked: backend.playPause()
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        hit: 48; size: 26
                        tint: "white"
                        path: "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"
                        onClicked: backend.nextTrack()
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        hit: 40; size: 20
                        path: backend.repeatMode === 1
                            ? "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4zm-4-2V9h-1l-2 1v1h1.5v4H13z"
                            : "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4z"
                        tint: backend.repeatMode !== 0 ? Glass.accentLight : Qt.rgba(1, 1, 1, 0.75)
                        marked: backend.repeatMode !== 0
                        onClicked: backend.cycleRepeat()
                    }
                }

                // The rest of the player: the heart, search, the + menu, sound, the words, settings.
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: timeTier.y + timeTier.height + 12 + 56 + 10
                    spacing: Math.max(6, Math.min(22, (dock.width - 6 * 40) / 7))
                    IconButton {
                        visible: backend.track !== ""
                        hit: 40; size: 20
                        tint: backend.songRating > 0 ? Glass.accentLight : Qt.rgba(1, 1, 1, 0.75)
                        path: backend.songRating > 0
                            ? "M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z"
                            : "M16.5 3c-1.74 0-3.41.81-4.5 2.09C10.91 3.81 9.24 3 7.5 3 4.42 3 2 5.42 2 8.5c0 3.78 3.4 6.86 8.55 11.54L12 21.35l1.45-1.32C18.6 15.36 22 12.28 22 8.5 22 5.42 19.58 3 16.5 3zm-4.4 15.55l-.1.1-.1-.1C7.14 14.24 4 11.39 4 8.5 4 6.5 5.5 5 7.5 5c1.54 0 3.04.99 3.57 2.36h1.87C13.46 5.99 14.96 5 16.5 5c2 0 3.5 1.5 3.5 3.5 0 2.89-3.14 5.74-7.9 10.05z"
                        onClicked: cinema.heartRequested()
                    }
                    IconButton {
                        hit: 40; size: 20
                        tint: Qt.rgba(1, 1, 1, 0.75)
                        path: "M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"
                        onClicked: cinema.toggleMenu("search")
                    }
                    IconButton {
                        hit: 40; size: 20
                        tint: Qt.rgba(1, 1, 1, 0.75)
                        path: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"
                        onClicked: cinema.toggleMenu("plus")
                    }
                    IconButton {
                        hit: 40; size: 20
                        tint: effects.enabled ? Glass.accentLight : Qt.rgba(1, 1, 1, 0.75)
                        path: "M3 17v2h6v-2H3zM3 5v2h10V5H3zm10 14v2h8v-2h-8zM7 9v2H3v2h4v2h2V9H7zm14 4v-2H11v2h10zm-6-4h2V7h4V5h-4V3h-2v6z"
                        onClicked: cinema.toggleMenu("sound")
                    }
                    IconButton {
                        hit: 40; size: 20
                        tint: cinema.lyricsOn ? Glass.accentLight : Qt.rgba(1, 1, 1, 0.75)
                        marked: cinema.lyricsOn
                        path: "M4 5h16v2H4zm0 4h10v2H4zm0 4h16v2H4zm0 4h10v2H4z"
                        onClicked: cinema.toggleLyrics()
                    }
                    IconButton {
                        hit: 40; size: 18
                        tint: Qt.rgba(1, 1, 1, 0.75)
                        path: "M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.488.488 0 0 0-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54a.484.484 0 0 0-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"
                        onClicked: cinema.toggleMenu("settings")
                    }
                }
            }
        }
    }

    // ---- right: the words ----
    LyricsView {
        id: words
        x: cinema.width * 0.52 + 90 * Math.max(0, Math.min(1, cinema.lf))
        y: 24
        width: cinema.width * 0.42
        height: cinema.height - 48
        opacity: 1 - Math.max(0, Math.min(1, cinema.lf * 1.6))
        visible: opacity > 0.01
        fontSize: Math.max(26, Math.min(56, cinema.height * 0.05))
    }

    // ---- the menus ----
    Item {
        id: menus
        anchors.fill: parent
        // Over the controls, centred on them, never past the screen's edges.
        readonly property real centerX: dockArea.x + dockArea.width / 2
        readonly property real above: dockArea.y - 14
        function px(w) { return Math.max(24, Math.min(cinema.width - w - 24, centerX - w / 2)) }
        function py(h) { return Math.max(24, above - h) }

        // A tap anywhere else closes the menu.
        MouseArea {
            anchors.fill: parent
            enabled: cinema.menu !== ""
            onClicked: cinema.closeMenu()
        }
        component Pop: Item {
            id: pop
            property bool shown: false
            property real radius: 22
            // The same liquid glass as the controls, behind the menu.
            LiquidGlass {
                anchors.fill: parent
                radius: pop.radius
                source: backdrop
                backX: pop.x
                backY: pop.y
                live: pop.visible
            }
            opacity: shown ? 1 : 0
            scale: shown ? 1 : 0.94
            visible: opacity > 0.01
            transformOrigin: Item.Bottom
            Behavior on opacity { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
            Behavior on scale { SpringAnimation { spring: Glass.springPanel; damping: Glass.dampingPanel; epsilon: 0.002 } }
        }

        Pop {
            shown: cinema.menu === "sound"
            width: 400
            height: soundPop.height
            x: menus.px(width)
            y: menus.py(height)
            AdvancedSoundStudio {
                id: soundPop
                width: parent.width
                height: implicitHeight
                source: backdrop
                backX: parent.x
                backY: parent.y
                bare: true
                onCloseRequested: cinema.closeMenu()
            }
        }
        Pop {
            shown: cinema.menu === "settings"
            width: 380
            height: settingsPop.height
            x: menus.px(width)
            y: menus.py(height)
            SettingsPanel {
                id: settingsPop
                width: parent.width
                source: backdrop
                backX: parent.x
                backY: parent.y
                bare: true
                open: cinema.menu === "settings"
                maxHeight: Math.min(560, menus.above - 24)
                visualStyle: cinema.player ? cinema.player.visualStyle : "liquid"
                visualStyles: cinema.player ? cinema.player.visualStyles : []
                showFollow: cinema.player ? cinema.player.showFollow : false
                onStyleChosen: function (id) { if (cinema.player) cinema.player.visualStyle = id }
            }
        }
        Pop {
            shown: cinema.menu === "search"
            width: 440
            height: Math.min(searchPop.wantHeight, menus.above - 24)
            Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            x: menus.px(width)
            y: menus.py(height)
            SearchPanel {
                id: searchPop
                anchors.fill: parent
                source: backdrop
                backX: parent.x
                backY: parent.y
                bare: true
                open: cinema.menu === "search"
                onCloseRequested: cinema.closeMenu()
            }
        }
        Pop {
            shown: cinema.menu === "plus"
            width: plusPop.width
            height: plusPop.height
            x: menus.px(width)
            y: menus.py(height)
            ActionDropdown {
                id: plusPop
                bare: true
                open: cinema.menu === "plus"
                onDismissed: cinema.closeMenu()
            }
        }
    }

    // ---- leave button, top right ----
    IconButton {
        id: leave
        x: cinema.width - width - 28
        y: 28
        hit: 40
        size: 20
        tint: "white"
        opacity: cinema.dockShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 280 } }
        path: "M5 16h3v3h2v-5H5v2zm3-8H5v2h5V5H8v3zm6 11h2v-3h3v-2h-5v5zm2-11V5h-2v5h5V8h-3z"
        onClicked: cinema.exitRequested()
    }
}
