import QtQuick
import QtQuick.Effects

// Synced lyrics: the line being sung sits in the middle of the view, big and white; the others step back.
//
// Active line   scale 1.06, pure white and sharp; the word being sung hops up with a springy bounce and glows
// Other lines   by distance from the middle of the view: 0.95 / 45 % / 3 px near it, down to 0.86 / 8 % /
//               32 px of blur at the top and bottom edges, where the words are a haze you cannot read
// Scrolling     the view follows the active line on a spring (Glass.springPanel / dampingPanel), so it glides and
//               settles with a little momentum. Touch the wheel and it lets go for a few seconds, then returns.
// Clicking      a line seeks to that line's start, to the millisecond
// Unsynced      plain lyrics are just shown, evenly lit, and scroll by hand
Item {
    id: view

    property real fontSize: 22
    // The song position in seconds. Bound by the parent so tests can drive it.
    property real position: backend.position
    readonly property var model: backend.lyrics
    readonly property bool synced: model.synced
    readonly property var lines: model.lines
    // The line being sung. It only moves back when the position really jumps back (a seek), never for the
    // small wobble of the player's clock around a line boundary, which made the view stutter.
    // How lifted a word is (0 at rest, 1 up, a little past either way while it bounces) at `now`, for a word
    // sung from `start` to `end` (all in ms). The rise and the settle scale with the word's length.
    function envelope(now, start, end) {
        if (now < start) return 0
        const length = Math.max(60, end - start)
        const rise = Math.min(170, Math.max(70, length * 0.35))
        const fall = Math.min(420, Math.max(160, length * 0.55))
        if (now < start + rise) return backOut((now - start) / rise)
        if (now < end) return 1
        if (now < end + fall) return 1 - backOut((now - end) / fall)
        return 0
    }
    // One letter's motion: `rise` (0 rest, 1 up, a little past while it bounces), `lean` (degrees, tipping
    // into the wave as it passes) and `lit` (0 dim, 1 white: letters light up as the crest reaches them).
    // Letter `index` of `count`, in a word sung from `start` to `end` (ms), at `now`.
    // How expressed a word is, 0..1, from how long it is held: under about 0.4 s it is a quick word (0); by about
    // 0.9 s, or when each letter gets more than about 0.2 s, it is fully sung out (1).
    function expression(start, end, letters) {
        const length = end - start
        const byTime = (length - 380) / 520
        const perLetter = (length / Math.max(1, letters) - 140) / 120
        return Math.max(0, Math.min(1, Math.max(byTime, perLetter)))
    }

    function letterMotion(now, start, end, index, count, amount) {
        if (amount === undefined) amount = 1
        const length = Math.max(60, end - start)
        const centre = (index + 0.5) / Math.max(1, count)
        const width = Math.max(0.28, Math.min(0.6, 1.6 / Math.max(1, count)))   // short words: one broad hump
        const p = Math.max(0, Math.min(1, (now - start) / length))
        const crest = -width + p * (1 + 2 * width)
        const offset = (centre - crest) / width
        const wave = now < start ? 0 : Math.exp(-offset * offset * 1.6)
        const whole = envelope(now, start, end)                   // the word's own hop, with its bounce
        const settle = now >= end ? whole : 1                     // after the word, everything settles together
        // `amount` scales the wave: a word held a little gets a gentle ripple, a word sung out the full wave.
        const rise = settle * (0.28 * whole + 0.85 * wave * amount)
        // Letters ahead of the crest lean back towards it, the ones it has passed lean forward.
        const lean = -7 * offset * wave * settle * amount
        const lit = now >= end ? 1 : Math.max(0, Math.min(1, (crest + width * 0.4 - centre) / 0.15 + 0.5))
        return { rise: rise, lean: lean, lit: now < start - 40 ? 0 : lit }
    }

    // Ease-out with a small overshoot (about 8 %), the "bounce".
    function backOut(t) {
        const c1 = 1.2
        const c3 = c1 + 1
        const u = t - 1
        return 1 + c3 * u * u * u + c1 * u * u
    }

    property int active: -1
    function refresh() {
        if (!synced) { active = -1; return }
        const index = model.indexAt(clock)
        if (index >= active || active < 0 || clock * 1000 < model.startOf(active) - 700)
            active = index
    }

    // A smooth clock for the words. The player's position arrives in steps and is corrected now and then,
    // which made lines and words flick forward and back. This one runs on the frame clock, never backwards
    // while playing, and leans gently towards the real position; only a real jump (a seek) is followed at once.
    property real clock: 0
    // What is heard lags what is played by the speaker's delay (Bluetooth: about 180 ms). The same delay the
    // animations use (Sound > Animation sync) holds the words back, so they land with the voice, not before it.
    readonly property real heardLag: (typeof audio !== "undefined" && audio) ? audio.syncMs / 1000 : 0
    readonly property real heard: position - heardLag
    readonly property bool runningClock: backend.playing && view.visible
    FrameAnimation {
        running: view.runningClock && view.synced
        onTriggered: {
            const error = view.heard - view.clock
            if (Math.abs(error) > 1.5) {
                view.clock = view.heard
            } else {
                // 0.6x to 1.4x real speed, so it catches up within a second or so and never stops or reverses.
                const rate = Math.max(0.6, Math.min(1.4, 1 + error * 0.9))
                view.clock += frameTime * rate
            }
            view.refresh()
        }
    }
    onPositionChanged: {
        // Seeks, a pause, and anything set from outside (tests) are followed at once.
        // (Worked out here rather than read from `heard`, whose binding may not have caught up yet.)
        const target = position - heardLag
        if (!runningClock || Math.abs(target - clock) > 1.5) {
            clock = target
            refresh()
        }
    }
    onLinesChanged: { active = -1; refresh() }
    onSyncedChanged: refresh()
    Component.onCompleted: refresh()
    // Fades the first and last lines into the background.
    property bool fadeEdges: true
    signal seekRequested(real seconds)

    function seekToLine(index) {
        const ms = model.startOf(index)
        if (ms < 0) return
        // Played that much earlier than heard, so the line starts when its first word reaches your ears.
        const target = ms / 1000 + view.heardLag
        view.seekRequested(target)
        backend.seek(target)
    }

    // ---- states with no words ----
    Text {
        anchors.centerIn: parent
        width: parent.width - 40
        horizontalAlignment: Text.AlignHCenter
        visible: !view.model.hasLyrics
        text: view.model.status === "loading" ? qsTr("Finding the words…")
            : view.model.status === "none" ? qsTr("No lyrics for this song")
            : qsTr("Lyrics appear here")
        color: "#8cffffff"
        font.family: Glass.font
        font.pixelSize: Math.max(14, view.fontSize * 0.8)
        wrapMode: Text.WordWrap
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: stack.height + height   // half a page of air above and below, so any line can reach the middle
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: true
        visible: view.model.hasLyrics
        layer.enabled: view.fadeEdges && view.model.hasLyrics
        layer.effect: MultiEffect { maskEnabled: true; maskSource: edgeMask }

        // After the reader scrolls by hand, auto-follow pauses for a moment.
        property bool following: true
        Timer { id: resume; interval: 3500; onTriggered: scroller.following = true }
        onMovementStarted: { following = false; resume.restart() }
        WheelHandler {
            onWheel: { scroller.following = false; resume.restart() }
        }

        readonly property real target: {
            // itemAt() does not announce changes, so everything that moves a row is read here to re-run this.
            stack.height; rows.count; scroller.height; view.fontSize
            if (!view.synced || view.active < 0 || view.active >= rows.count) return 0
            const item = rows.itemAt(view.active)
            if (!item) return 0
            return Math.max(0, stack.y + item.y + item.height / 2 - scroller.height / 2)
        }
        // The spring: contentY glides to the target, with a little overshoot. It is a Behavior, driven by the
        // assignments below, so that it keeps working after it settles (a bound `running` breaks when it stops).
        Behavior on contentY {
            enabled: scroller.following && !scroller.dragging
            SpringAnimation { spring: Glass.springLyrics; damping: Glass.dampingLyrics; epsilon: 0.25 }
        }
        onTargetChanged: if (following && view.synced) contentY = target
        onFollowingChanged: if (following && view.synced) contentY = target

        Column {
            id: stack
            y: scroller.height / 2 - view.fontSize   // the first line starts near the middle
            width: scroller.width
            spacing: view.fontSize * 0.55

            Repeater {
                id: rows
                model: view.lines
                Item {
                    id: row
                    required property int index
                    required property var modelData
                    readonly property bool current: view.synced && index === view.active
                    readonly property int distance: view.synced && view.active >= 0 ? Math.abs(index - view.active) : 0
                    width: stack.width
                    // Wrapped at the widest the line ever gets (scale 1.06), so the layout never moves with the spring.
                    height: label.paintedHeight + 4
                    // The line as words, so the one being sung can lift. Wrapped at the widest the line ever gets
                    // (scale 1.06), so the layout never moves with the spring.
                    Flow {
                        id: label
                        width: parent.width / 1.06
                        readonly property real paintedHeight: height
                        // Depth of field, as in Apple Music: the line being sung is sharp and forward. The others
                        // soften with how far they sit from the middle of the view, not by count, so whatever is near
                        // the top and bottom edges is a haze you can see but not read (up to 32 px of blur, 8 %).
                        readonly property real edge: {
                            const centre = stack.y + row.y + row.height / 2 - scroller.contentY
                            return Math.min(1, Math.abs(centre - scroller.height / 2) / Math.max(1, scroller.height / 2))
                        }
                        readonly property real far: edge < 0.12 ? 0 : edge > 0.75 ? 1 : (function (t) { return t * t * (3 - 2 * t) })((edge - 0.12) / 0.63)
                        // 1 on the line being sung, eased in and out so a line comes into focus rather than snapping.
                        property real sharp: row.current ? 1 : 0
                        Behavior on sharp { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }
                        scale: !view.synced ? 1 : 1.06 * sharp + (0.95 - 0.09 * far) * (1 - sharp)
                        transformOrigin: Item.Left
                        opacity: !view.synced ? 1 : sharp + (0.45 - 0.37 * far) * (1 - sharp)
                        readonly property real blurPx: !view.synced ? 0 : (3 + 29 * far) * (1 - sharp)
                        layer.enabled: blurPx > 0.2
                        layer.effect: MultiEffect { blurEnabled: true; blurMax: 32; blur: label.blurPx / 32; autoPaddingEnabled: true }
                        Repeater {
                            model: row.modelData.words
                            // The Flow places each word's slot; the word moves inside it (the Flow owns x and y).
                            Item {
                                id: slot
                                required property var modelData
                                // Where this word is in the song, on the smooth clock: sung, being sung, or to come.
                                readonly property real nowMs: row.current ? view.clock * 1000 : 0
                                readonly property bool timed: slot.modelData.time >= 0
                                readonly property bool sung: row.current && timed && nowMs >= slot.modelData.end
                                readonly property bool singing: row.current && timed && nowMs >= slot.modelData.time - 40 && nowMs < slot.modelData.end
                                // How lifted the word is as a whole (glow strength).
                                readonly property real lift: row.current && timed ? view.envelope(nowMs, slot.modelData.time, slot.modelData.end) : 0
                                // How much this word is sung out: 0 for a quick word ("the", "and"), up to 1 for one that
                                // is held. Only expressed words get the letter-by-letter wave, scaled by this; quick ones
                                // get a light hop of the whole word, which is also far cheaper to draw.
                                readonly property real expression: timed ? view.expression(slot.modelData.time, slot.modelData.end, slot.modelData.text.length) : 0
                                readonly property bool letters: row.current && view.synced && timed && expression > 0.15
                                width: letters ? letterRow.implicitWidth : word.implicitWidth
                                height: word.implicitHeight

                                Text {
                                    id: word
                                    visible: !slot.letters
                                    text: slot.modelData.text + " "
                                    font.family: Glass.font
                                    font.pixelSize: view.fontSize
                                    font.weight: row.current ? Font.Bold : Font.DemiBold
                                    // On the line being sung: dim until its turn, white once reached.
                                    color: !view.synced || !row.current || !slot.timed ? (view.synced ? "#ffffff" : Qt.rgba(1, 1, 1, 0.82))
                                         : (slot.sung || slot.singing) ? "#ffffff" : Qt.rgba(1, 1, 1, 0.55)
                                    Behavior on color { ColorAnimation { duration: 120 } }
                                    // A quick word: a small, light hop of the whole word, timed to it.
                                    transform: Translate { y: row.current ? -view.fontSize * 0.045 * slot.lift : 0 }
                                }

                                // The wave: a crest runs through the word from its first letter to its last over exactly
                                // the time the word is sung. Each letter rises as the crest reaches it, leans into it,
                                // lights up, and comes back down once it has passed, so the left side lowers while the
                                // right side comes up. The whole word also rides up a little (with a bounce) while sung.
                                Row {
                                    id: letterRow
                                    visible: slot.letters
                                    layer.enabled: slot.letters && Math.abs(slot.lift) > 0.02
                                    layer.effect: MultiEffect {
                                        shadowEnabled: true
                                        shadowColor: "white"
                                        shadowOpacity: 0.5 * Math.max(0, Math.min(1, slot.lift))
                                        shadowBlur: 0.6
                                        blurMax: 24
                                        shadowVerticalOffset: 0
                                        shadowHorizontalOffset: 0
                                        autoPaddingEnabled: true
                                    }
                                    Repeater {
                                        model: slot.letters ? (slot.modelData.text + " ").split("") : []
                                        Text {
                                            id: letter
                                            required property int index
                                            required property string modelData
                                            readonly property int count: slot.modelData.text.length + 1
                                            readonly property var motion: view.letterMotion(slot.nowMs, slot.modelData.time, slot.modelData.end, index, count, slot.expression)
                                            text: modelData
                                            font.family: Glass.font
                                            font.pixelSize: view.fontSize
                                            font.weight: Font.Bold
                                            color: Qt.rgba(1, 1, 1, 0.55 + 0.45 * motion.lit)
                                            transform: [
                                                Rotation {
                                                    origin.x: letter.width / 2
                                                    origin.y: letter.height
                                                    angle: motion.lean
                                                },
                                                Scale {
                                                    origin.x: letter.width / 2
                                                    origin.y: letter.height
                                                    xScale: 1 + 0.06 * motion.rise
                                                    yScale: 1 + 0.10 * motion.rise
                                                },
                                                Translate { y: -view.fontSize * 0.13 * motion.rise }
                                            ]
                                        }
                                    }
                                }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: view.synced
                        cursorShape: view.synced ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: view.seekToLine(row.index)
                    }
                }
            }
        }
    }

    // Top and bottom edges dissolve into the backdrop.
    Item {
        id: edgeMask
        anchors.fill: scroller
        visible: false
        layer.enabled: true
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.14; color: "black" }
                GradientStop { position: 0.86; color: "black" }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }
}
