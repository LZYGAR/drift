import QtQuick
import QtTest
import ClickUi

Item {
    id: scene
    width: 360
    height: 640
    property string last: ""
    property int closed: 0

    CommandPalette {
        id: pal
        items: [
            { id: "mini", label: "Mini player", hint: "Ctrl+M", keywords: "compact small" },
            { id: "mute", label: "Mute", hint: "", keywords: "volume silence" },
            { id: "queue", label: "Up Next", hint: "U", keywords: "queue" }
        ]
        onTriggered: function (id, arg) { scene.last = id + ":" + arg }
        onDismissed: scene.closed += 1
    }

    TestCase {
        name: "CommandPalette"
        when: windowShown

        function init() {
            pal.open = false
            pal.open = true
            scene.last = ""
            scene.closed = 0
        }

        function test_opens_with_every_command() {
            compare(pal.results.length, 3)
            compare(pal.selected, 0)
        }

        function test_typing_filters_and_enter_runs_the_best_match() {
            keyClick(Qt.Key_M)
            keyClick(Qt.Key_U)
            compare(pal.query, "mu")
            compare(pal.results[0].id, "mute")
            keyClick(Qt.Key_Return)
            compare(scene.last, "mute:")
        }

        function test_keywords_find_a_command() {
            for (const key of [Qt.Key_Q, Qt.Key_U, Qt.Key_E, Qt.Key_U, Qt.Key_E])
                keyClick(key)
            compare(pal.results[0].id, "queue")
        }

        function test_typed_text_can_be_searched() {
            for (const key of [Qt.Key_D, Qt.Key_A, Qt.Key_F, Qt.Key_T])
                keyClick(key)
            const search = pal.results[pal.results.length - 1]
            compare(search.id, "search")
            compare(search.arg, "daft")
            // Up from the first row wraps to the last, which is the search.
            keyClick(Qt.Key_Up)
            compare(pal.selected, pal.results.length - 1)
            keyClick(Qt.Key_Return)
            compare(scene.last, "search:daft")
        }

        function test_mix_prefix_is_a_mood_for_grok() {
            compare(pal.mood("mix: synthwave rainy night"), "synthwave rainy night")
            compare(pal.mood("Make Mix: Rainy Sunday"), "Rainy Sunday")
            compare(pal.mood("make a mix slow jazz"), "slow jazz")
            compare(pal.mood("mixing desk"), "")
            compare(pal.mood("mute"), "")
        }

        function test_a_described_sound_is_offered_as_an_eq_setting() {
            if (!effects.available)
                skip("needs the PipeWire tools")
            for (const ch of "muffled club bathroom")
                keyClick(ch)
            compare(pal.results[0].id, "vibe")
            verify(pal.results[0].label.indexOf("muffled") >= 0)
            const shape = JSON.parse(pal.results[0].arg)
            compare(shape.bands.length, 10)
            verify(shape.reverb > 0.4)
            // A song title that happens to contain a sound word ("night") is still a search.
            pal.open = false
            pal.open = true
            for (const ch of "night drive")
                keyClick(ch)
            verify(pal.results[0].id !== "vibe", "first: " + pal.results[0].id)
            // Ordinary searches are left alone.
            pal.open = false
            pal.open = true
            for (const ch of "daft punk")
                keyClick(ch)
            for (const item of pal.results)
                verify(item.id !== "vibe")
        }

        function test_arrows_move_and_wrap() {
            keyClick(Qt.Key_Down)
            compare(pal.selected, 1)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(pal.selected, 0)
        }

        function test_escape_asks_to_close() {
            keyClick(Qt.Key_Escape)
            compare(scene.closed, 1)
        }
    }
}
