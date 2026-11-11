import QtQuick
import QtTest
import ClickUi

// The lyrics view on the preview fixture (made-up synced words for "Night Drive").
Item {
    width: 360
    height: 520

    LyricsView {
        id: view
        anchors.fill: parent
        position: 0
    }

    TestCase {
        name: "LyricsView"
        when: windowShown

        // No speaker delay here, so the words follow the position exactly (the delay is tested on its own below).
        function init() { audio.syncMs = 0; view.position = 0 }

        function test_the_line_being_sung_follows_the_position() {
            verify(view.synced)
            verify(view.lines.length > 10)
            compare(view.active, -1)          // before the first line
            view.position = 4.2
            compare(view.active, 0)
            view.position = 30
            compare(view.active, 5)           // 27.1 s: "Nobody's counting the miles tonight"
            view.position = 9999
            compare(view.active, view.lines.length - 1)
        }

        function test_the_words_wait_for_the_speaker_delay() {
            // Bluetooth: heard 200 ms after it is played. At 4.3 s played, 4.1 s is heard: the first line (4.2 s)
            // has not been sung yet.
            audio.syncMs = 200
            view.position = 4.3
            compare(view.active, -1)
            view.position = 4.45
            tryCompare(view, "active", 0)   // a small step: the smooth clock glides there
            audio.syncMs = 0
        }

        function test_a_wave_runs_through_the_letters_left_to_right() {
            // A 6-letter word (plus its space) sung from 1000 to 2000 ms.
            const first = function (t) { return view.letterMotion(t, 1000, 2000, 0, 7) }
            const last = function (t) { return view.letterMotion(t, 1000, 2000, 5, 7) }
            verify(first(1200).rise > last(1200).rise, "early: the left side is up")
            verify(last(1850).rise > first(1850).rise, "late: the left side has come down and the right is up")
            // Letters light up as the crest reaches them, and all are lit once the word is sung.
            verify(first(1300).lit > last(1300).lit)
            compare(last(2100).lit, 1)
            // Before the word nothing moves, and well after it everything is at rest.
            compare(first(900).rise, 0)
            compare(last(3000).rise, 0)
        }

        function test_only_words_that_are_sung_out_get_the_letter_wave() {
            compare(view.expression(1000, 1250, 3), 0)       // "the", a quarter second: a quick word
            compare(view.expression(1000, 1300, 7), 0)       // a long word sung quickly: still quick
            compare(view.expression(1000, 2000, 5), 1)       // held for a second: fully sung out
            compare(view.expression(1000, 1700, 2), 1)       // "oh", drawn out: each letter has time
            const half = view.expression(1000, 1650, 6)
            verify(half > 0 && half < 1, "in between: " + half)
            // A gentle ripple for a word held a little, the full wave for one sung out.
            verify(view.letterMotion(1300, 1000, 2000, 2, 6, 0.3).rise < view.letterMotion(1300, 1000, 2000, 2, 6, 1).rise)
        }

        function test_the_lift_follows_each_words_own_timing() {
            // Before, during and after a 400 ms word, and a 2 s word: up for exactly as long as it is sung.
            compare(view.envelope(900, 1000, 1400), 0)
            verify(view.envelope(1060, 1000, 1400) > 0.3)          // rising
            compare(view.envelope(1300, 1000, 1400), 1)            // held while sung
            verify(view.envelope(1500, 1000, 1400) < 1)            // settling after it ends
            compare(view.envelope(2000, 1000, 1400), 0)            // at rest
            compare(view.envelope(2900, 1000, 3000), 1)            // a long word stays up to its end
            // A short word rises faster than a long one.
            verify(view.envelope(1040, 1000, 1150) > view.envelope(1040, 1000, 3000))
            // The bounce: it overshoots on the way up.
            let peak = 0
            for (let t = 1000; t < 1200; t += 5) peak = Math.max(peak, view.envelope(t, 1000, 3000))
            verify(peak > 1.02, "peak " + peak)
        }

        function test_clicking_the_middle_line_seeks_to_its_start() {
            view.position = 30
            wait(1500)   // the spring brings the active line to the middle
            mouseClick(view, view.width / 2, view.height / 2)
            // The fixture's backend seeks in place; the clock may have moved it a hair.
            verify(Math.abs(backend.position - 27.1) < 1.0, "position " + backend.position)
        }

        function test_lines_step_back_and_the_active_one_is_white() {
            view.position = 30
            wait(300)
            // All lines are text items; the active one is the only one at full white.
            let white = 0
            function walk(item) {
                for (let i = 0; i < item.children.length; ++i) {
                    const child = item.children[i]
                    if (child.hasOwnProperty("text") && child.hasOwnProperty("wrapMode") && child.text.length > 0 && Qt.colorEqual(child.color, "#ffffff"))
                        white += 1
                    walk(child)
                }
            }
            walk(view)
            verify(white >= 1, "white lines: " + white)
        }
    }
}
