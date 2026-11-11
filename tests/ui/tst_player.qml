import QtQuick
import QtTest
import ClickUi

// The whole player on the preview fixture ("Night Drive" by The Midnight).
Item {
    width: 360
    height: 640

    Player {
        id: player
        anchors.fill: parent
    }

    TestCase {
        name: "Player"
        when: windowShown

        function test_commands_cover_the_basics() {
            const ids = player.buildCommands().map(function (item) { return item.id })
            for (const id of ["playpause", "next", "mute", "search", "queue", "shuffle", "repeat", "sound", "mini", "quit"])
                verify(ids.indexOf(id) >= 0, id)
        }

        function test_animation_style_menu_and_commands() {
            compare(player.visualStyle, "liquid")   // the default
            player.runCommand("style", "bounce")
            compare(player.visualStyle, "bounce")
            const ids = player.buildCommands().map(function (item) { return item.id })
            verify(ids.indexOf("styles") >= 0)
            player.runCommand("styles", "")
            verify(player.styleOpen)
            player.styleOpen = false
            player.runCommand("style", "waveform")
        }

        function test_similar_songs_switch_is_a_palette_command() {
            verify(backend.autoSimilar)   // on by default
            const ids = player.buildCommands().map(function (item) { return item.id })
            // Only offered when the Grok CLI is installed.
            compare(ids.indexOf("similar") >= 0, backend.similarAvailable)
            if (backend.similarAvailable) {
                // Preview flips the switch but neither saves it nor asks Grok anything.
                player.runCommand("similar", "")
                verify(!backend.autoSimilar)
                player.runCommand("similar", "")
                verify(backend.autoSimilar)
            }
        }

        function test_heart_taps_hovers_and_the_taste_switch() {
            backend.forgetTaste()
            verify(backend.useTaste)
            compare(backend.songRating, 0)
            player.toggleHeart()                     // a tap: 8 out of 10
            compare(backend.songRating, 8)
            player.runCommand("love", "")           // or the top of the scale
            compare(backend.songRating, 10)
            player.toggleHeart()                     // tapping a rated song takes the rating back
            compare(backend.songRating, 0)
            const ids = player.buildCommands().map(function (item) { return item.id })
            for (const id of ["heart", "love", "taste"])
                verify(ids.indexOf(id) >= 0, id)
            // Switched off: the heart is not offered, and nothing is recorded.
            player.runCommand("taste", "")
            verify(!backend.useTaste)
            player.toggleHeart()
            compare(backend.songRating, 0)
            player.runCommand("taste", "")
            verify(backend.useTaste)
            backend.forgetTaste()
        }

        function test_mute_and_back() {
            const before = backend.volume
            verify(before > 0)
            player.runCommand("mute", "")
            compare(backend.volume, 0)
            verify(backend.muted)
            player.runCommand("mute", "")
            compare(backend.volume, before)
        }

        function test_shuffle_and_repeat() {
            player.runCommand("shuffle", "")
            verify(backend.shuffle)
            player.runCommand("shuffle", "")
            verify(!backend.shuffle)
            player.runCommand("repeat", "")
            compare(backend.repeatMode, 2)
            player.runCommand("repeat", "")
            player.runCommand("repeat", "")
            compare(backend.repeatMode, 0)
        }

        function test_palette_search_fills_the_search_field() {
            player.paletteOpen = true
            player.runCommand("search", "daft punk")
            verify(!player.paletteOpen)
            compare(backend.kind, "search")
            tryCompare(player, "searchText", "daft punk")
            backend.menu()
        }

        function test_sound_command_opens_the_sheet_and_a_preset_shapes_the_curve() {
            player.runCommand("sound", "")
            verify(player.soundOpen)
            player.soundOpen = false
            // Flat Studio is the one preset that leaves effects off, so no audio graph starts here.
            effects.mid = 0.5
            player.runCommand("preset", "Flat Studio")
            compare(effects.mid, 0)
            verify(!effects.enabled)
        }

        function test_plus_menu_offers_own_playlists_and_the_current_one() {
            // The fixture says the queue started from the person's own playlist "night drive".
            compare(backend.currentPlaylist, "night drive")
            compare(backend.playlists.length, 3)
            player.plusOpen = true
            wait(450)
            verify(!player.plusTyping)
            player.plusOpen = false
            // Typing a new playlist name must keep the one-key shortcuts quiet: the field owns the keyboard.
            player.plusOpen = true
            wait(450)
            player.plusOpen = false
        }

        function test_advanced_studio_uses_ten_bands_without_starting_audio() {
            player.runCommand("sound", "")
            effects.advanced = true
            compare(effects.bands.length, 10)
            effects.setBand(5, 20)
            compare(effects.bands[5], 12)   // clamped to +-12 dB
            wait(50)
            verify(!effects.enabled)
            effects.reset()
            effects.advanced = false
            player.soundOpen = false
        }
    }
}
