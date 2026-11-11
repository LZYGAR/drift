import QtQuick
import QtQuick.Effects

// The opening: the word "drift" draws itself in frosted black-and-white glass, glows, then either dissolves into the player (you
// are signed in) or glides up to its place on the welcome screen (you are not).
//
// It waits for the app to know which: if Apple Music has not answered after `patience` seconds, it is the welcome.
// `still` (for screenshots) freezes it at a moment, 0 to 1 of the draw-on.
Item {
    id: intro

    property bool signedIn: false
    property bool busy: false          // the sign-in window is open
    property real patience: 15
    property bool known: true          // Apple Music has said whether you are signed in
    property real still: -1
    // Where the word rests on the welcome screen.
    property real welcomeY: 70
    property real welcomeWidth: 220
    signal finished()
    signal arrivedAtWelcome()

    // 0 draw-on, 1 held, 2 leaving
    property real drawn: 0
    property real leave: 0              // 0 to 1: dissolve (signed in) or glide (signed out)
    property bool going: false
    property bool goingHome: false      // glide to the welcome spot rather than dissolve
    property real waited: 0
    // 1 while the word pours in, easing to 0 as it sets; it melts again as it leaves.
    property real molten: 1

    property bool active: true
    visible: active && (leave < 1 || still >= 0)
    enabled: visible
    // Swallows clicks while it plays.
    MouseArea { anchors.fill: parent; hoverEnabled: true }

    Rectangle {
        id: veil
        anchors.fill: parent
        color: "#050506"
        opacity: 1 - intro.leave
    }
    // a soft vignette of colour behind the word
    LiquidField {
        anchors.fill: parent
        running: intro.visible
        primary: "#ffffff"
        secondary: "#8d8d95"
        ambient: "#050506"
        opacity: 0.85 * (intro.goingHome ? 1 - intro.leave : (1 - intro.leave)) * Math.min(1, intro.drawn * 1.5)
    }

    DriftMark {
        id: word
        running: intro.visible
        progress: intro.still >= 0 ? intro.still : intro.drawn
        readonly property real bigWidth: Math.min(intro.width - 60, 300)
        markWidth: bigWidth + (intro.welcomeWidth - bigWidth) * (intro.goingHome ? intro.leave : 0)
        x: (intro.width - width) / 2
        y: (intro.height - height) / 2 - 30 + (intro.goingHome ? (intro.welcomeY - ((intro.height - height) / 2 - 30)) * intro.leave : 0)
        // dissolving: swells and clears
        scale: intro.goingHome ? 1 : 1 + 0.5 * intro.leave
        opacity: intro.goingHome ? 1 : 1 - intro.leave
        glow: 1 + 0.8 * Math.sin(Math.min(1, intro.drawn) * Math.PI)
        liquid: intro.still >= 0 ? (intro.still < 1 ? 0.8 : 0) : intro.molten
    }
    // the line under it
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: word.y + word.height + 18
        text: intro.busy ? qsTr("Signing in…") : qsTr("your music, drifting")
        color: "#a8a8b0"
        font.family: Glass.font
        font.pixelSize: 14
        font.letterSpacing: 3
        font.capitalization: Font.AllUppercase
        opacity: (intro.goingHome ? 1 - intro.leave : 1 - intro.leave) * Math.max(0, Math.min(1, (intro.drawn - 0.7) / 0.3))
    }

    // The timeline.
    SequentialAnimation {
        id: timeline
        running: intro.still < 0 && intro.visible
        NumberAnimation { target: intro; property: "drawn"; from: 0; to: 1; duration: 1700; easing.type: Easing.InOutCubic }
        NumberAnimation { target: intro; property: "molten"; from: 1; to: 0; duration: 900; easing.type: Easing.OutCubic }
        PauseAnimation { duration: 250 }
        ScriptAction { script: { intro.going = true; check.start() } }
    }
    // Once drawn, wait until the sign-in state is known (up to `patience`), then leave.
    Timer {
        id: check
        interval: 100
        repeat: true
        onTriggered: {
            intro.waited += 0.1
            if (intro.signedIn || (intro.known && !intro.busy) || (!intro.busy && intro.waited >= intro.patience)) {
                stop()
                intro.goingHome = !intro.signedIn
                if (!intro.goingHome) melt.start()
                leaving.start()
            }
        }
    }
    NumberAnimation { id: melt; target: intro; property: "molten"; to: 1; duration: 700; easing.type: Easing.InCubic }
    NumberAnimation {
        id: leaving
        target: intro
        property: "leave"
        from: 0
        to: 1
        duration: 750
        easing.type: Easing.InOutCubic
        onFinished: { if (intro.goingHome) intro.arrivedAtWelcome(); intro.finished() }
    }
}
