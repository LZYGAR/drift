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
    onActiveChanged: if (active) poke()

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
        }
        // Darker towards the words, so they stay easy to read over any cover.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#14000000" }
                GradientStop { position: 0.55; color: "#2e000000" }
                GradientStop { position: 1.0; color: "#59000000" }
            }
        }
    }

    // ---- left: the cover, with the transport under it ----
    readonly property real dockHeight: 140
    readonly property real gap: 28          // cover to title
    readonly property real titleHeight: 66  // 28 px title, 16 px artist
    readonly property real gap2: 22         // title to the controls
    Item {
        id: coverBox
        x: cinema.width * 0.05
        y: 0
        width: cinema.width * 0.43
        height: cinema.height
        readonly property real below: cinema.gap + cinema.titleHeight + cinema.gap2 + cinema.dockHeight
        readonly property real side: Math.max(120, Math.min(width * 0.8, height - 110 - below))
        readonly property real groupTop: (height - (side + below)) / 2

        // The cover breathes with the bass through a spring, as in the player and the mini player, a little
        // more here because there is room for it: up to 6 %.
        property real pump: Glass.reactive ? 0.06 * Glass.cover : 0
        Behavior on pump { SpringAnimation { spring: 9; damping: 0.38; epsilon: 0.0005 } }
        readonly property real glowLevel: !backend.playing ? 0.15 : Glass.reactive ? Math.min(1, 0.25 + 0.75 * Glass.cover) : 0.35

        Item {
            id: art
            x: (coverBox.width - coverBox.side) / 2
            y: coverBox.groupTop
            width: coverBox.side
            height: coverBox.side
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
                opacity: coverBox.glowLevel * 0.85
                scale: 1.04 + (Glass.reactive ? 0.16 * Glass.cover : 0.03)
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
            x: (coverBox.width - coverBox.side) / 2
            y: coverBox.groupTop + coverBox.side + cinema.gap
            width: coverBox.side
            height: cinema.titleHeight
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
                    font.pixelSize: 28
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: titleBlock.shownArtist
                    elide: Text.ElideRight
                    color: Qt.rgba(1, 1, 1, 0.65)
                    font.family: Glass.font
                    font.pixelSize: 16
                    font.weight: Font.Medium
                }
            }
        }

        // ---- the controls: a card of liquid glass, the time bar on top and the transport under it ----
        Item {
            id: dockArea
            x: (coverBox.width - coverBox.side) / 2
            y: titleBlock.y + titleBlock.height + cinema.gap2
            width: coverBox.side
            height: cinema.dockHeight
            scale: 1 + 0.012 * (Glass.reactive ? Glass.cover : 0)
            Behavior on scale { SpringAnimation { spring: 9; damping: 0.38; epsilon: 0.0005 } }

            // Liquid glass: the backdrop behind the card, blurred and brightened, with the liquid moving inside it.
            Item {
                id: dock
                anchors.fill: parent
                readonly property real radius: 36
                Rectangle { id: dockMask; anchors.fill: parent; radius: dock.radius; visible: false; layer.enabled: true }
                // A soft shadow so the card floats.
                Rectangle {
                    anchors.fill: parent
                    radius: dock.radius
                    color: "black"
                    layer.enabled: true
                    layer.effect: MultiEffect { shadowEnabled: true; shadowColor: "black"; shadowOpacity: 0.3; shadowBlur: 1.0; blurMax: 48; shadowVerticalOffset: 12; autoPaddingEnabled: true }
                }
                ShaderEffectSource {
                    id: dockGrab
                    anchors.fill: parent
                    visible: false
                    sourceItem: backdrop
                    sourceRect: Qt.rect(coverBox.x + dockArea.x, coverBox.y + dockArea.y, dock.width, dock.height)
                    live: cinema.active
                    textureSize: Qt.size(Math.ceil(dock.width / 2), Math.ceil(dock.height / 2))
                }
                MultiEffect {
                    anchors.fill: parent
                    source: dockGrab
                    blurEnabled: true
                    blur: 1.0
                    blurMax: 48
                    saturation: 0.35
                    brightness: 0.0
                    autoPaddingEnabled: false
                    maskEnabled: true
                    maskSource: dockMask
                }
                // The same liquid as the backdrop, moving inside the glass, a little faster.
                Item {
                    anchors.fill: parent
                    layer.enabled: true
                    layer.effect: MultiEffect { maskEnabled: true; maskSource: dockMask }
                    LiquidField {
                        anchors.fill: parent
                        running: cinema.active
                        pace: 0.8
                        opacity: 0.45
                    }
                }
                // A smoky tint so white controls always read over a bright cover, then gloss and rim: the glass itself.
                Rectangle { anchors.fill: parent; radius: dock.radius; color: Qt.rgba(0, 0, 0, 0.22) }
                Rectangle {
                    anchors.fill: parent
                    radius: dock.radius
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#2effffff" }
                        GradientStop { position: 0.4; color: "transparent" }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: dock.radius
                    color: "transparent"
                    border.width: 1
                    border.color: "#38ffffff"
                }
                // The rim lights up with the bass.
                Rectangle {
                    anchors.fill: parent
                    radius: dock.radius
                    color: "transparent"
                    border.width: 1.5
                    border.color: Glass.accentLight
                    opacity: 0.12 + 0.5 * (Glass.reactive ? Math.min(1, Glass.cover) : 0)
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
            }
        }
    }

    // ---- right: the words ----
    LyricsView {
        id: words
        x: cinema.width * 0.52
        y: 24
        width: cinema.width * 0.42
        height: cinema.height - 48
        fontSize: Math.max(26, Math.min(56, cinema.height * 0.05))
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
