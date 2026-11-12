pragma Singleton
import QtQuick

// The design tokens for the whole player: colours, glass, blur and springs.
// Change a number here and every screen follows.
QtObject {
    id: glass

    // ---- text and surfaces (neutral white on black) ----
    // "Follow Omarchy theme" (Settings): the palette comes from the active theme's colors.toml.
    // A light theme keeps the dark glass (the glass needs a dark base to read), so only dark themes recolour the text and base.
    readonly property bool followTheme: backend.themeFollow && !theme.light
    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
    readonly property color ink: followTheme ? theme.foreground : "#f5f5f7"          // primary text and icons
    readonly property color mute: followTheme ? withAlpha(Qt.color(theme.foreground), 0.65) : "#a6f2f2f5"       // secondary text, 65%
    readonly property color faint: followTheme ? withAlpha(Qt.color(theme.foreground), 0.36) : "#5cf2f2f5"      // hints and times, 36%
    readonly property color inkText: followTheme ? theme.background : "#0c0c0d"      // text on an ink-filled button
    readonly property color base: followTheme ? theme.background : "#0b0b0c"         // behind everything
    readonly property color fill: "#1fffffff"       // glass fill at rest, 12%
    readonly property color fillHover: "#33ffffff"  // glass fill under the pointer, 20%
    readonly property color rail: "#38ffffff"       // unplayed waveform and slider rails, 22%
    readonly property color specular: "#24ffffff"   // 1px edge highlight, rgba(255,255,255,0.14)
    readonly property color sheen: "#1affffff"      // top-edge gloss on panels, 10%
    readonly property color panelShade: "#73101010" // a light dark tint over the blur (45%), so text stays readable
    readonly property color glassMilk: "#14ffffff"  // the glass itself: white at 8%
    // The rim: lit from the top-left, rgba(255,255,255,0.22) falling to 0.04 at the bottom-right.
    readonly property color rimBright: "#38ffffff"
    readonly property color rimMiddle: "#1affffff"
    readonly property color rimDim: "#0affffff"
    readonly property color scrim: "#8c000000"      // behind sheets and the palette, 55%
    // "Use Omarchy font" (Settings) swaps Inter for the font Omarchy has set, if this machine has it.
    readonly property string font: backend.fontFollow && theme.fontFamily !== "" ? theme.fontFamily : "Inter"

    // ---- glass ----
    readonly property int panelBlur: 48             // blur radius behind panels, px
    readonly property real panelSaturation: 0.3     // blurred backdrop is a little richer
    readonly property int panelRadius: 26
    readonly property real shadowOpacity: 0.45     // rgba(0,0,0,0.45)
    readonly property int shadowOffset: 24         // "24dp" of elevation
    readonly property int shadowBlur: 32           // px

    // ---- motion ----
    // Springs for anything that pops: buttons, chips, the time capsule.
    readonly property real springBouncy: 4.0
    readonly property real dampingBouncy: 0.22
    // Panels and sheets: quicker to settle, one soft overshoot.
    // The brief's spring {stiffness 450, damping 22, mass 1} is a physical model: zeta = c / (2 sqrt(k m)) = 0.52.
    // SpringAnimation is frame based, so the numbers do not carry over; spring ~ k / 100 and damping ~ zeta * 0.42
    // reproduce the same overshoot (about 15 %) next to springBouncy, which was tuned by eye. Treat as calibrated, not exact.
    readonly property real springPill: 4.5
    readonly property real dampingPill: 0.22
    // Lyrics rolling into the middle: {stiffness 320, damping 26, mass 1}, zeta 0.73, by the same rule as the pill.
    readonly property real springLyrics: 3.2
    readonly property real dampingLyrics: 0.31
    readonly property real springPanel: 3.0
    readonly property real dampingPanel: 0.28
    // Switches and presses: a high-tension snap with one small overshoot.
    readonly property real springSnap: 7.0
    readonly property real dampingSnap: 0.32
    // The window itself, when it morphs to the mini player: no visible wobble.
    readonly property real springWindow: 3.4
    readonly property real dampingWindow: 0.42
    readonly property int colorShift: 800           // ms, backdrop colours between songs

    // ---- the music itself (see audiolevels.h) ----
    // True while the player can hear its own audio. Otherwise the interface
    // animates on its own, as before.
    readonly property bool reactive: audio.active
    readonly property real level: audio.level      // how loud, 0 to 1
    readonly property real bass: audio.bass        // 40 to 100 Hz
    readonly property real mid: audio.mid          // 150 Hz to 2 kHz
    readonly property real treble: audio.treble    // 2 kHz and up
    readonly property real kick: audio.beat        // 1 on every beat, then fades in about 200 ms
    readonly property real cover: audio.cover      // how hard the album cover moves: bass-led, or sensitive without bass
    readonly property var bands: audio.spectrum    // 14 bands, 40 Hz to 14 kHz

    // One shared clock for everything that moves continuously (the backdrop
    // drift and the waveform ripple). A single 20 Hz tick means one redraw per
    // tick instead of one per timer, and when `live` is false nothing redraws.
    // The frame clock for the music: once per drawn frame, what the player hears is moved on by the
    // time that really passed. This, not a timer, is what keeps the motion smooth on any screen.
    property FrameAnimation frames: FrameAnimation {
        running: glass.live
        onTriggered: audio.advance()
    }

    property bool live: false
    property int beat: 0
    readonly property int beatsPerSecond: 20
    property Timer ticker: Timer {
        interval: 1000 / glass.beatsPerSecond
        repeat: true
        running: glass.live
        onTriggered: glass.beat += 1
    }

    // ---- colour that follows the cover ----
    readonly property color themeAccent: theme.accent
    // With nothing playing the app is black and white; a song brings its cover's colours in.
    // Following the theme: the idle look is the theme's, and a song's colours are pulled 25% toward it so the cover still leads.
    readonly property real themePull: followTheme ? 0.25 : 0
    property color accent: backend.hasArtColor ? mix(backend.artColor, theme.accent, themePull)
                         : followTheme ? theme.accent : "#a9a9b0"
    property color secondary: backend.hasArtColor ? mix(backend.artSecondary, theme.selection, themePull)
                            : followTheme ? theme.selection : "#45454a"
    property color ambient: backend.hasArtColor ? mix(backend.artAmbient, theme.background, themePull)
                          : followTheme ? theme.background : "#0e0e10"
    Behavior on accent { ColorAnimation { duration: glass.colorShift; easing.type: Easing.InOutSine } }
    Behavior on secondary { ColorAnimation { duration: glass.colorShift; easing.type: Easing.InOutSine } }
    Behavior on ambient { ColorAnimation { duration: glass.colorShift; easing.type: Easing.InOutSine } }
    readonly property color accentLight: Qt.lighter(accent, 1.25)

    function mix(a, b, t) {
        // Colours often arrive as strings ("#rrggbb"); read them as colours first or the channels are undefined.
        a = Qt.color(a); b = Qt.color(b)
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t)
    }

    function clock(seconds) {
        const whole = Math.max(0, Math.round(seconds))
        const m = Math.floor(whole / 60)
        const s = whole % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }
}
