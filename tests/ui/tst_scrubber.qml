import QtQuick
import QtTest
import ClickUi

Item {
    id: scene
    width: 360
    height: 200
    property var seeks: []

    // In the app this is the frame clock (Glass.frames); the tests have no Player, so they run their own.
    FrameAnimation {
        running: true
        onTriggered: audio.advance()
    }

    LiquidScrubber {
        id: bar
        x: 24
        y: 20
        width: 312
        duration: 200
        position: 50
        trackKey: "Night Drive\u0000The Midnight"
        onSeekRequested: function (seconds) { scene.seeks.push(seconds) }
    }

    TestCase {
        name: "LiquidScrubber"
        when: windowShown

        readonly property real midY: bar.waveTop + bar.waveHeight / 2

        function init() {
            scene.seeks = []
        }

        function test_one_bar_per_four_pixels_and_a_peak_for_each() {
            compare(bar.count, 78)
            compare(bar.peaks.length, 78)
            compare(bar.shown, 0.25)
        }

        function test_hover_puts_the_bead_under_the_pointer() {
            mouseMove(bar, 78, midY)
            verify(bar.hovered)
            compare(bar.beadAt, 0.25)
            tryVerify(function () { return bar.focusAmount > 0.9 })
            // nothing is sought by hovering
            compare(scene.seeks.length, 0)
        }

        function test_shift_click_seeks_exactly_there() {
            mouseClick(bar, 156, midY, Qt.LeftButton, Qt.ShiftModifier)
            compare(scene.seeks.length, 1)
            compare(scene.seeks[0], 100)
            verify(!bar.dragging)
        }

        function test_dragging_sticks_to_a_quiet_dip() {
            // Find a spot just beside a dip in this song's shape.
            let from = -1, to = -1
            for (let x = 8; x < 304 && from < 0; x += 1) {
                const ratio = x / 312
                const pulled = waveform.snap(bar.peaks, ratio, 0.012)
                if (Math.abs(pulled - ratio) > 0.002) {
                    from = x
                    to = pulled
                }
            }
            verify(from > 0, "this waveform has a dip to stick to")
            mousePress(bar, 20, midY)
            verify(bar.dragging)
            mouseMove(bar, from, midY)
            verify(bar.snapped)
            fuzzyCompare(bar.dragTarget, to, 0.0001)
            mouseRelease(bar, from, midY)
            verify(!bar.dragging)
            verify(!bar.snapped)
            compare(scene.seeks.length, 1)
            fuzzyCompare(scene.seeks[0], to * 200, 0.01)
        }

        function test_pulling_past_the_end_stretches_then_lets_go() {
            mousePress(bar, 300, midY, Qt.LeftButton, Qt.ShiftModifier)
            mouseMove(bar, 420, midY, -1, Qt.LeftButton, Qt.ShiftModifier)
            compare(bar.dragTarget, 1)
            verify(bar.stretch > 5 && bar.stretch <= 22, "stretch is " + bar.stretch)
            mouseRelease(bar, 420, midY, Qt.LeftButton, Qt.ShiftModifier)
            compare(scene.seeks[0], 200)
            tryVerify(function () { return Math.abs(bar.stretch) < 0.1 })
        }

        function test_every_style_draws_the_bars_and_still_seeks() {
            for (const style of ["waveform", "bounce", "dots", "minimal"]) {
                bar.style = style
                bar.playing = true
                Glass.beat += 1
                mouseClick(bar, 78, midY, Qt.LeftButton, Qt.ShiftModifier)
                fuzzyCompare(scene.seeks[scene.seeks.length - 1], 50, 0.5, style)
            }
            bar.style = "waveform"
            bar.playing = false
        }

        // The bars follow the music, not a loop: they move with the (made-up) groove
        // the test gives the player, and are still when it is quiet.
        function test_bars_follow_the_audio() {
            audio.wanted = true
            tryVerify(function () { return Glass.reactive })
            bar.playing = true
            for (const style of ["liquid", "bounce", "waveform", "dots"]) {
                bar.style = style
                tryVerify(function () { return bar.reactive })
                // Let the springs settle from the change of style first, so only the music can move the bars.
                wait(900)
                let low = 1e9, high = -1e9
                // The groove has a kick every half second, so a second covers two.
                for (let i = 0; i < 70; ++i) {
                    wait(16)
                    // The waveform's reaction is strongest around the playhead (bar 19 of 78).
                    const probe = style === "waveform" || style === "liquid" ? 19 : 4
                    const h = style === "dots" ? bar.barY(probe) : bar.barHeight(probe)
                    low = Math.min(low, h)
                    high = Math.max(high, h)
                }
                verify(high - low > 8, style + " moved only " + (high - low))
            }
            // With nothing to hear, there is no reaction: the interface animates on its own.
            audio.wanted = false
            tryVerify(function () { return !Glass.reactive })
            tryCompare(audio, "bass", 0, 3000)
            bar.style = "waveform"
            bar.playing = false
        }

        function test_bars_settle_when_paused() {
            bar.playing = true
            tryCompare(bar, "energy", 1, 2000)
            bar.playing = false
            tryCompare(bar, "energy", 0, 2000)
            // The bars finish their last bit of motion and come to rest...
            tryVerify(function () {
                const before = bar.barHeight(19)
                wait(120)
                return before === bar.barHeight(19)
            }, 4000)
            // ...and then nothing moves at all: nothing is drawing.
            const rest = bar.barHeight(19)
            wait(250)
            compare(bar.barHeight(19), rest)
        }
    }
}
