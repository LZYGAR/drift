import QtQuick
import ClickNative

// The time bar: a waveform you can grab.
//
// Playing   The played part ripples: two sine waves of different speeds travel
//           along its bars. Paused, the ripple settles and the bars stand still.
// Hover     A glowing bead follows the pointer, the bars swell around it, and
//           a glass capsule above shows the time left from that spot (-M:SS).
// Dragging  The playhead follows the pointer on a spring, sticks lightly to
//           the quiet dips between phrases (hold Shift to switch that off),
//           and stretches a little past either end before springing back.
//
// Apple's audio is DRM-protected, so the shape comes from `waveform` (see
// waveform.h): a song-like arc seeded from the track, the same every time.
Item {
    id: scrubber

    property real position: 0        // seconds
    property real duration: 0        // seconds
    property bool playing: false
    property string trackKey: ""
    property color accent: Glass.accent
    // The small version for the mini player: no labels and no capsule.
    property bool compact: false
    // How the bar moves while a song plays: "liquid", "waveform", "bounce", "dots" or "minimal".
    property string style: "liquid"
    // For screenshots: a ratio from 0 to 1 shows the bead there, as if hovered.
    property real pin: -1
    // The context object `audio`, under another name: inside WaveBars, plain `audio` is its own property.
    readonly property var audioSource: audio
    signal seekRequested(real seconds)

    readonly property real barWidth: compact ? 2 : 2.5
    readonly property real barGap: 1.5
    // The compact bar is 18 px tall unless the parent asks for more (the full-screen view uses 32).
    property real waveHeight: compact ? 18 : 46
    readonly property real waveTop: compact ? 2 : 34
    readonly property int count: Math.max(8, Math.floor((width + barGap) / (barWidth + barGap)))
    implicitHeight: compact ? waveHeight + 4 : waveTop + waveHeight + 24

    // Rebuilt only when the song or the width changes.
    readonly property var peaks: waveform.peaks(trackKey, duration, count)

    // True while the music itself is moving the bar (see audiolevels.h).
    readonly property bool reactive: Glass.reactive && playing && energy > 0.01
    // For tests and screenshots: where the bars are right now.
    function barHeight(i) { return bars.barHeight(i) }
    function barY(i) { return bars.barCentre(i) }

    readonly property real liveRatio: duration > 0 ? Math.max(0, Math.min(1, position / duration)) : 0
    readonly property bool hovered: area.containsMouse
    property bool dragging: false
    property bool snapped: false
    property real hoverRatio: 0
    property real dragTarget: 0
    // How far past an end the pointer has pulled, in pixels (already softened).
    property real stretch: 0
    Behavior on stretch {
        enabled: !scrubber.dragging
        SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.05 }
    }

    // Drawn only while a song plays and the bar is showing.
    readonly property bool ticking: playing && visible

    // What is drawn as "played". It follows the song, and follows a drag
    // through a spring, which is what makes the playhead feel elastic.
    property real shown: dragging ? dragTarget : liveRatio
    Behavior on shown {
        enabled: scrubber.dragging
        SpringAnimation { spring: 5.0; damping: 0.38; epsilon: 0.0005 }
    }

    // Where the bead sits: on the playhead while dragging, under the pointer on hover.
    readonly property bool engaged: hovered || dragging || pin >= 0
    readonly property real beadAt: dragging ? shown : (hovered || pin < 0 ? hoverRatio : pin)
    property real focusAmount: engaged ? 1 : 0
    Behavior on focusAmount {
        SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 }
    }

    // The ripple fades in and out with this, so the bars settle when a song is paused.
    property real energy: playing ? 1 : 0
    Behavior on energy { NumberAnimation { duration: 600; easing.type: Easing.InOutSine } }

    readonly property color playedColor: Glass.mix(Qt.lighter(scrubber.accent, 1.25), Glass.ink, 0.3)

    // ---- the waveform: one native item draws every bar (see wavebars.h) ----
    WaveBars {
        id: bars
        y: scrubber.waveTop
        width: parent.width
        height: scrubber.waveHeight
        peaks: scrubber.peaks
        style: scrubber.style
        barWidth: scrubber.barWidth
        barGap: scrubber.barGap
        shown: scrubber.shown
        beadAt: scrubber.beadAt
        focusAmount: scrubber.focusAmount
        energy: scrubber.energy
        playedColor: scrubber.playedColor
        railColor: Glass.rail
        animating: scrubber.ticking
        audio: scrubber.audioSource
    }

    // ---- the liquid bead ----
    Item {
        id: bead
        visible: scrubber.focusAmount > 0.01
        x: scrubber.beadAt * scrubber.width + scrubber.stretch
        y: scrubber.waveTop + scrubber.waveHeight / 2
        scale: scrubber.focusAmount * (scrubber.dragging ? 1.25 : 1)
        Behavior on scale {
            SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 }
        }
        readonly property real size: scrubber.compact ? 8 : 12
        // Glow: two soft rings. It brightens while the playhead is held by a quiet dip.
        Rectangle {
            anchors.centerIn: parent
            width: bead.size * 3.2 * (1 + (scrubber.reactive ? 0.6 * Glass.kick : 0))
            height: width
            radius: width / 2
            color: scrubber.accent
            opacity: scrubber.snapped ? 0.3 : 0.16
            Behavior on opacity { NumberAnimation { duration: 140 } }
        }
        Rectangle {
            anchors.centerIn: parent
            width: bead.size * 1.9
            height: width
            radius: width / 2
            color: scrubber.accent
            opacity: scrubber.snapped ? 0.6 : 0.38
            Behavior on opacity { NumberAnimation { duration: 140 } }
        }
        // The bead itself stretches sideways when pulled past an end.
        Rectangle {
            anchors.centerIn: parent
            width: bead.size + Math.abs(scrubber.stretch) * 0.6
            height: bead.size
            radius: height / 2
            color: Glass.ink
            border.color: "#66ffffff"
            border.width: 1
        }
    }

    // ---- the time capsule ----
    Item {
        id: capsule
        visible: !scrubber.compact && scrubber.focusAmount > 0.01
        width: capsuleText.implicitWidth + 22
        height: 24
        x: Math.max(0, Math.min(scrubber.width - width, scrubber.beadAt * scrubber.width + scrubber.stretch - width / 2))
        y: scrubber.dragging ? -2 : 2
        Behavior on y { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy } }
        opacity: Math.min(1, scrubber.focusAmount)
        scale: 0.7 + 0.3 * scrubber.focusAmount
        transformOrigin: Item.Bottom
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: "#99181310"
            border.color: Glass.specular
            border.width: 1
        }
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            gradient: Gradient {
                GradientStop { position: 0; color: Glass.sheen }
                GradientStop { position: 0.6; color: "transparent" }
            }
        }
        Text {
            id: capsuleText
            anchors.centerIn: parent
            text: "-" + Glass.clock(Math.max(0, scrubber.duration * (1 - scrubber.beadAt)))
            color: Glass.ink
            font.family: Glass.font
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.features: { "tnum": 1 }
        }
    }

    // ---- times ----
    Text {
        visible: !scrubber.compact
        y: scrubber.waveTop + scrubber.waveHeight + 8
        text: Glass.clock(scrubber.duration * scrubber.shown)
        color: Glass.faint
        font.family: Glass.font
        font.pixelSize: 11
        font.features: { "tnum": 1 }
    }
    Text {
        visible: !scrubber.compact
        y: scrubber.waveTop + scrubber.waveHeight + 8
        anchors.right: parent.right
        text: "-" + Glass.clock(Math.max(0, scrubber.duration * (1 - scrubber.shown)))
        color: Glass.faint
        font.family: Glass.font
        font.pixelSize: 11
        font.features: { "tnum": 1 }
    }

    MouseArea {
        id: area
        x: 0
        y: scrubber.waveTop - 8
        width: parent.width
        height: scrubber.waveHeight + 16
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // Quiet dips within 1.2% of the song pull the playhead in.
        readonly property real reach: 0.012

        function follow(mouse) {
            const raw = mouse.x / width
            const inside = Math.max(0, Math.min(1, raw))
            scrubber.hoverRatio = inside
            if (!pressed)
                return
            // Past an end the pull gets weaker the further you go, like rubber.
            const over = (raw - inside) * width
            scrubber.stretch = Math.sign(over) * 22 * (1 - Math.exp(-Math.abs(over) / 60))
            const magnet = (mouse.modifiers & Qt.ShiftModifier) ? inside : waveform.snap(scrubber.peaks, inside, reach)
            scrubber.snapped = Math.abs(magnet - inside) > 0.0001
            scrubber.dragTarget = magnet
        }
        onPositionChanged: function (mouse) { follow(mouse) }
        onPressed: function (mouse) {
            scrubber.dragTarget = scrubber.liveRatio
            scrubber.dragging = true
            follow(mouse)
        }
        onReleased: {
            if (scrubber.duration > 0)
                scrubber.seekRequested(scrubber.duration * scrubber.dragTarget)
            finish()
        }
        onCanceled: finish()
        function finish() {
            scrubber.dragging = false
            scrubber.snapped = false
            scrubber.stretch = 0
        }
    }
}
