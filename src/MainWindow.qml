import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtWebEngine
import QtWebChannel
import QtCore
import Qt.labs.platform as Platform
import ClickUi

ApplicationWindow {
    id: win
    // Full player, or a compact bar you can park in a corner.
    property bool mini: false
    // Cinema: borderless full screen with the big cover and the lyrics (FullScreenLyricsView.qml).
    property bool cinema: false
    function setCinema(on) {
        if (on === cinema)
            return
        if (on) {
            if (!backend.signedIn || backend.signingIn)
                return
            // (A mini player stays mini underneath: the window's own height is left alone while full screen.)
            // The size limits lift first; otherwise the window refuses to grow past 360 px.
            cinema = true
            visibility = Window.FullScreen
        } else {
            // Leave full screen first; only then do the 360 px limits and size bindings come back.
            visibility = Window.Windowed
            cinema = false
        }
    }
    readonly property int fullHeight: 640
    readonly property int miniHeight: 96
    // 0 is the full player, 1 the mini player. The window morphs between the
    // two on a spring that is damped enough not to wobble the window frame.
    property real morph: mini ? 1 : 0
    Behavior on morph {
        // CLICK_PREVIEW_PANEL=morph flips mini mode once, to watch the resize. A preview that can be carried
        // between desktops (CLICK_DESKTOP_TEST) animates like the real app, so what is tested is what runs.
        // While carried along there is no animation at all: Hyprland sets the size itself, and each in-between
        // height the window passed through would make it re-centre.
        enabled: win.positionReady && !desktop.away && (!backend.preview || backend.previewPanel === "morph" || desktop.available)
        SpringAnimation { spring: Glass.springWindow; damping: Glass.dampingWindow; epsilon: 0.002 }
    }
    readonly property int shownHeight: Math.round(fullHeight - (fullHeight - miniHeight) * Math.max(0, Math.min(1, morph)))
    // The size follows the mini/full morph, except in cinema, where the compositor owns it.
    width: 360
    height: shownHeight
    Binding { target: win; property: "width"; value: 360; when: !win.cinema; restoreMode: Binding.RestoreNone }
    Binding { target: win; property: "height"; value: win.shownHeight; when: !win.cinema; restoreMode: Binding.RestoreNone }
    minimumWidth: cinema ? 0 : 360
    minimumHeight: cinema ? 0 : shownHeight
    maximumWidth: cinema ? 16777215 : 360
    maximumHeight: cinema ? 16777215 : shownHeight
    visible: true
    title: "Music Player"
    color: Glass.base
    flags: Qt.Window | Qt.FramelessWindowHint | (staysOnTop ? Qt.WindowStaysOnTopHint : 0)

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // Text fields own the keyboard on these screens, so single-letter shortcuts stand down.
    readonly property bool typing: backend.kind === "search" || backend.kind === "mix" || player.paletteOpen || player.plusTyping
    // The palette's own field handles the arrows and Enter while it is open; so does the + menu's new-playlist field.
    readonly property bool listKeys: !backend.signingIn && !player.paletteOpen && !player.plusOpen
    property bool faceOpened: false
    property string authHost: ""
    property bool staysOnTop: false
    // Stays false until the saved place has been applied, so the first 0,0 does not clobber it.
    property bool positionReady: false

    // A Wayland client cannot place its own window. Setting x/y is skipped there.
    function waylandOwnsPosition() {
        const name = Qt.platform.pluginName || ""
        return name.indexOf("wayland") === 0
    }

    function restorePosition() {
        if (waylandOwnsPosition() || !windowSettings.havePosition)
            return
        x = windowSettings.windowX
        y = windowSettings.windowY
    }

    function rememberPosition() {
        if (!positionReady || backend.preview || waylandOwnsPosition())
            return
        windowSettings.windowX = Math.round(x)
        windowSettings.windowY = Math.round(y)
        windowSettings.havePosition = true
        windowSettings.sync()
    }

    function rememberStaysOnTop() {
        if (!positionReady || backend.preview)
            return
        windowSettings.staysOnTop = staysOnTop
        windowSettings.sync()
    }

    function rememberMini() {
        // Not while the player is carried along as the mini player: quitting then must not
        // bring it back as the mini player next time.
        if (!positionReady || backend.preview || desktop.away)
            return
        windowSettings.mini = mini
        windowSettings.sync()
    }
    onMiniChanged: rememberMini()

    // Switching desktops carries the player along as the mini player (see desktop.h).
    Binding { target: desktop; property: "playing"; value: backend.playing && !win.cinema }
    Binding { target: desktop; property: "mini"; value: win.mini }
    Connections {
        target: desktop
        function onMiniRequested(mini) { win.mini = mini }
    }

    Connections {
        target: player
        function onVisualStyleChanged() {
            if (!win.positionReady || backend.preview)
                return
            windowSettings.visualStyle = player.visualStyle
            windowSettings.sync()
        }
    }

    onXChanged: rememberPosition()
    onYChanged: rememberPosition()
    onStaysOnTopChanged: rememberStaysOnTop()
    onClosing: rememberPosition()

    Settings {
        id: windowSettings
        // Preview grabs an offscreen frame and must not rewrite the saved window.
        location: "file://" + (backend.preview ? "/tmp/click-window-preview.conf" : backend.dataPath + "/window.conf")
        category: "window"
        property bool staysOnTop: false
        property bool mini: false
        property int windowX: 0
        property int windowY: 0
        property bool havePosition: false
        property string visualStyle: "liquid"

        Component.onCompleted: {
            // Children complete before the window is shown, so this lands before the first frame.
            win.staysOnTop = staysOnTop
            win.mini = backend.previewMini || mini
            // CLICK_PREVIEW_PANEL=cinema shows the full-screen view (a screenshot cannot go real full screen).
            if (backend.previewPanel === "cinema") { win.cinema = true; win.width = 1280; win.height = 720 }
            if (!backend.preview) player.visualStyle = visualStyle
            win.restorePosition()
            win.positionReady = true
        }
    }

    Timer {
        interval: 1200
        running: backend.previewPanel === "morph"
        onTriggered: win.mini = !win.mini
    }

    // CLICK_DESKTOP_TEST=1 CLICK_PREVIEW_PANEL=desktop: carries this preview window away, then home, as a
    // desktop switch would, to watch the geometry. (It pins and moves the window; nothing else does that in preview.)
    Timer {
        interval: 1500
        running: backend.previewPanel === "desktop"
        onTriggered: desktop.simulate(true)
    }
    Timer {
        interval: 7000
        running: backend.previewPanel === "desktop"
        onTriggered: desktop.simulate(false)
    }

    // The animated face is the home screen. Library stays put after they open it.
    Timer {
        interval: 700
        running: !backend.preview && backend.signedIn && !win.faceOpened
        onTriggered: {
            win.faceOpened = true
            if (!win.typing)
                backend.openPlayer()
        }
    }

    // While Apple's page is up, these would swallow Enter and the arrow keys.
    Shortcut { sequence: "?"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "A"; enabled: !backend.signingIn && backend.kind === "list"; context: Qt.ApplicationShortcut; onActivated: backend.addToQueue(backend.selected, false) }
    Shortcut { sequence: "N"; enabled: !backend.signingIn && backend.kind === "list"; context: Qt.ApplicationShortcut; onActivated: backend.addToQueue(backend.selected, true) }
    Shortcut { sequence: "U"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.openQueue() }
    Shortcut { sequence: "M"; enabled: !backend.signingIn && !win.typing && backend.mixAvailable; context: Qt.ApplicationShortcut; onActivated: backend.openMix() }
    Shortcut { sequence: "Q"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.playPause() }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        // Closes whatever is on top first: cinema, the palette, then Sound, then one screen back.
        onActivated: {
            if (win.cinema) win.setCinema(false)
            else if (player.paletteOpen) player.paletteOpen = false
            else if (player.lyricsOpen) player.lyricsOpen = false
            else if (player.plusOpen) player.plusOpen = false
            else if (player.styleOpen) player.styleOpen = false
            else if (player.soundOpen) player.soundOpen = false
            else if (win.helpVisible) win.helpVisible = false
            else backend.menu()
        }
    }
    Shortcut {
        sequence: "Ctrl+K"
        enabled: !backend.signingIn && backend.signedIn
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.mini) win.mini = false
            player.soundOpen = false
            player.styleOpen = false
            player.paletteOpen = !player.paletteOpen
        }
    }
    Shortcut {
        sequence: "Backspace"
        enabled: !backend.signingIn && !player.paletteOpen && (!win.typing || player.searchVisible && player.searchText.length === 0 || player.mixVisible && player.mixText.length === 0)
        context: Qt.ApplicationShortcut
        onActivated: backend.menu()
    }
    Shortcut { sequence: "Return"; enabled: win.listKeys; context: Qt.ApplicationShortcut; onActivated: backend.center() }
    Shortcut { sequence: "Enter"; enabled: win.listKeys; context: Qt.ApplicationShortcut; onActivated: backend.center() }
    Shortcut { sequence: "Up"; enabled: win.listKeys; context: Qt.ApplicationShortcut; onActivated: backend.wheel(-1) }
    Shortcut { sequence: "Down"; enabled: win.listKeys; context: Qt.ApplicationShortcut; onActivated: backend.wheel(1) }
    Shortcut { sequence: "Left"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.previousTrack() }
    Shortcut { sequence: "Right"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.nextTrack() }
    Shortcut { sequence: "-"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.nudgeVolume(-1) }
    Shortcut { sequence: "="; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.nudgeVolume(1) }
    Shortcut { sequence: "F11"; enabled: !backend.signingIn && backend.signedIn; context: Qt.ApplicationShortcut; onActivated: win.setCinema(!win.cinema) }
    Shortcut { sequence: "Ctrl+M"; enabled: !backend.signingIn && !win.cinema; context: Qt.ApplicationShortcut; onActivated: win.mini = !win.mini }
    Shortcut { sequence: "H"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: player.toggleHeart() }
    Shortcut { sequence: "T"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: win.staysOnTop = !win.staysOnTop }

    // The Apple page stays loaded after sign-in, shrunk away, so the song keeps playing.
    Loader {
        anchors.fill: parent
        active: !backend.preview
        z: backend.signingIn ? 5 : 0
        sourceComponent: sessionComponent
    }

    Component {
        id: sessionComponent
        Item {
            id: session
            anchors.fill: parent

            WebEngineProfile {
                id: musicProfile
                storageName: "click"
                persistentStoragePath: backend.dataPath + "/web"
                cachePath: backend.dataPath + "/cache"
                offTheRecord: false
                persistentCookiesPolicy: WebEngineProfile.ForcePersistentCookies
                httpCacheType: WebEngineProfile.DiskHttpCache
                httpUserAgent: "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.7339.264 Safari/537.36"
            }

            property bool wasSigningIn: false
            property int homeTrips: 0
            property var popups: []

            function hostOf(url) {
                const match = String(url).match(/^https?:\/\/([^\/]+)/)
                return match ? match[1] : ""
            }

            function closePopups() {
                const open = popups.slice()
                popups = []
                for (const popup of open)
                    popup.close()
            }

            // Runs before Apple's scripts. Passkey detection and blob workers hang this engine.
            function installAuthFix(view) {
                const fix = WebEngine.script()
                fix.name = "authfix"
                fix.injectionPoint = WebEngineScript.DocumentCreation
                fix.worldId = WebEngineScript.MainWorld
                fix.runsOnSubFrames = true
                fix.sourceUrl = "qrc:/authfix.js"
                view.userScripts.insert(fix)
            }

            // Apple's account page talks back to the music page. Opening it in
            // the same view leaves the phone step spinning forever.
            function openPopup(request) {
                const popup = authPopupComponent.createObject(win)
                popup.show()
                request.openIn(popup.view)
                const next = popups.slice()
                next.push(popup)
                popups = next
                popup.requestActivate()
                popup.view.forceActiveFocus()
            }

            Component {
                id: authPopupComponent
                Window {
                    id: popupWindow
                    width: 520
                    height: 760
                    minimumWidth: 420
                    minimumHeight: 640
                    visible: false
                    title: "Sign in"
                    color: "#ffffff"
                    modality: Qt.ApplicationModal
                    property alias view: popupWeb

                    WebEngineView {
                        id: popupWeb
                        anchors.fill: parent
                        profile: musicProfile
                        backgroundColor: "#ffffff"
                        settings.javascriptEnabled: true
                        settings.localStorageEnabled: true
                        settings.javascriptCanOpenWindows: true
                        onNewWindowRequested: function (request) { session.openPopup(request) }
                        onWindowCloseRequested: popupWindow.close()
                        onUrlChanged: win.authHost = session.hostOf(url)
                        onTitleChanged: popupWindow.title = popupWeb.title || "Sign in"
                        Component.onCompleted: session.installAuthFix(popupWeb)
                    }

                    onClosing: {
                        const next = session.popups.filter(function (popup) { return popup !== popupWindow })
                        session.popups = next
                        Qt.callLater(popupWindow.destroy)
                    }
                }
            }

            WebChannel {
                id: channel
                Component.onCompleted: registerObject("bridge", backend.bridge)
            }

            WebEngineView {
                id: web
                profile: musicProfile
                webChannel: channel
                // Stays full size so the song keeps reporting, even while the
                // window is the mini player. Covered once signed in.
                width: parent.width
                height: backend.signingIn ? parent.height - 72 : win.fullHeight
                opacity: backend.signingIn ? 1 : 0
                backgroundColor: Glass.base
                url: "https://music.apple.com/"
                settings.javascriptEnabled: true
                settings.playbackRequiresUserGesture: false
                settings.javascriptCanOpenWindows: true
                settings.localStorageEnabled: true
                lifecycleState: WebEngineView.LifecycleState.Active
                onNewWindowRequested: function (request) { session.openPopup(request) }
                onUrlChanged: {
                    const host = session.hostOf(url)
                    win.authHost = host
                    // Sign-in belongs in the popup. If the player page wanders off, bring it home.
                    if (backend.signingIn || session.homeTrips > 2)
                        return
                    if (host !== "accounts.google.com" && host !== "appleid.apple.com")
                        return
                    session.homeTrips += 1
                    web.url = "https://music.apple.com/"
                }
                // WebEngineScript is a value type in this Qt, so the page scripts are built here.
                Component.onCompleted: {
                    const channelScript = WebEngine.script()
                    channelScript.name = "qwebchannel"
                    channelScript.injectionPoint = WebEngineScript.DocumentCreation
                    channelScript.worldId = WebEngineScript.MainWorld
                    channelScript.sourceUrl = "qrc:/qwebchannel.js"
                    const podScript = WebEngine.script()
                    podScript.name = "pod"
                    podScript.injectionPoint = WebEngineScript.DocumentReady
                    podScript.worldId = WebEngineScript.MainWorld
                    podScript.sourceUrl = "qrc:/pod.js"
                    userScripts.insert(channelScript)
                    userScripts.insert(podScript)
                    session.installAuthFix(web)
                }
            }

            Connections {
                target: backend
                function onCommandReady() {
                    if (!backend.command)
                        return
                    web.runJavaScript(backend.command)
                }
                function onSessionChanged() {
                    if (backend.signingIn && !session.wasSigningIn)
                        web.forceActiveFocus()
                    if (!backend.signingIn && session.wasSigningIn)
                        session.closePopups()
                    session.wasSigningIn = backend.signingIn
                }
            }
        }
    }

    Timer {
        interval: 2200
        running: backend.shotPath !== ""
        onTriggered: (win.cinema ? cinemaView : player).grabToImage(function (result) {
            result.saveToFile(backend.shotPath)
            Qt.quit()
        })
    }

    // A companion in the system tray: what is playing, the transport, and a few
    // jumps through the song, without bringing the window forward.
    Platform.SystemTrayIcon {
        id: tray
        // CLICK_PREVIEW_PANEL=tray shows it in preview too, to test the menu.
        visible: available && backend.signedIn && (!backend.preview || backend.previewPanel === "tray")
        icon.name: "click"
        icon.source: "qrc:/click.svg"
        tooltip: backend.track !== "" ? backend.track + " · " + backend.artist : "Music Player"
        // The time in the menu moves in 5 second steps, so the tray is not redrawn ten times a second.
        readonly property real coarse: Math.floor(backend.position / 5) * 5
        function reveal() {
            win.show()
            win.raise()
            win.requestActivate()
        }
        onActivated: function (reason) {
            if (reason === Platform.SystemTrayIcon.MiddleClick)
                backend.playPause()
            else if (reason !== Platform.SystemTrayIcon.Context)
                reveal()
        }
        menu: Platform.Menu {
            Platform.MenuItem {
                enabled: false
                text: backend.track !== "" ? backend.track + " · " + backend.artist : qsTr("Nothing playing")
            }
            Platform.MenuItem {
                enabled: false
                visible: backend.duration > 0
                text: backend.formatClock(tray.coarse) + " / " + backend.durationLabel
            }
            Platform.MenuSeparator {}
            Platform.MenuItem { text: backend.playing ? qsTr("Pause") : qsTr("Play"); onTriggered: backend.playPause() }
            Platform.MenuItem { text: qsTr("Next"); onTriggered: backend.nextTrack() }
            Platform.MenuItem { text: qsTr("Previous"); onTriggered: backend.previousTrack() }
            Platform.MenuSeparator {}
            Platform.MenuItem { text: qsTr("Back 15 seconds"); enabled: backend.duration > 0; onTriggered: backend.seekBy(-15) }
            Platform.MenuItem { text: qsTr("Forward 30 seconds"); enabled: backend.duration > 0; onTriggered: backend.seekBy(30) }
            Platform.MenuItem { text: qsTr("Start over"); enabled: backend.duration > 0; onTriggered: backend.seek(0) }
            Platform.MenuSeparator {}
            Platform.MenuItem { text: qsTr("Show player"); onTriggered: tray.reveal() }
            Platform.MenuItem { text: qsTr("Quit"); onTriggered: Qt.quit() }
        }
    }

    Player {
        id: player
        anchors.fill: parent
        z: 2
        visible: !backend.signingIn && !win.cinema
        window: win
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
    }

    FullScreenLyricsView {
        id: cinemaView
        z: 3
        active: win.cinema
        onExitRequested: win.setCinema(false)
    }

    Column {
        visible: backend.signingIn
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 2
        z: 6
        Text {
            width: parent.width
            text: qsTr("Finish sign-in in Apple's window. Esc cancels.")
            color: "#d0d0d4"
            elide: Text.ElideRight
            font.family: "Inter"
            font.pixelSize: 13
        }
        Text {
            width: parent.width
            visible: win.authHost !== ""
            text: win.authHost
            color: "#8e8e94"
            elide: Text.ElideRight
            font.family: "Inter"
            font.pixelSize: 11
        }
        Text {
            width: parent.width
            visible: backend.status !== ""
            text: backend.status
            color: "#d7c4a1"
            elide: Text.ElideRight
            font.family: "Inter"
            font.pixelSize: 12
        }
    }

    Rectangle {
        visible: win.helpVisible
        anchors.fill: parent
        color: "#c8000000"
        z: 8
        MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

        Column {
            anchors.centerIn: parent
            spacing: 8
            Text { text: "Music Player"; color: "white"; font.family: "Inter"; font.pixelSize: 22; font.weight: Font.Bold }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Scroll or ↑ ↓   move") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Click or Enter   choose") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Esc   back") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Space   play or pause") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("← →   previous or next") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("− =   volume") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("A   add to Up Next") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("N   play next") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("U   Up Next") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("M   make a mix with Grok") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Ctrl+K   run any command") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("?   shortcuts") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("H   heart the song") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("T   always on top") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Ctrl+M   mini player") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("F11   full screen with lyrics") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Q   quit") }
        }
    }
}
