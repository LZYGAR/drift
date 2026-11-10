import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtWebEngine
import QtWebChannel
import Qt.labs.settings
import ClickUi

ApplicationWindow {
    id: win
    width: 360
    height: 640
    minimumWidth: 360
    minimumHeight: 640
    maximumWidth: 360
    maximumHeight: 640
    visible: true
    title: "Click"
    color: "#14110c"
    flags: Qt.Window | Qt.FramelessWindowHint | (staysOnTop ? Qt.WindowStaysOnTopHint : 0)

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    // Text fields own the keyboard on these screens, so single-letter shortcuts stand down.
    readonly property bool typing: backend.kind === "search" || backend.kind === "mix"
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

    onXChanged: rememberPosition()
    onYChanged: rememberPosition()
    onStaysOnTopChanged: rememberStaysOnTop()
    onClosing: rememberPosition()

    Settings {
        id: windowSettings
        // Preview grabs an offscreen frame and must not rewrite the saved window.
        fileName: backend.preview ? "/tmp/click-window-preview.conf" : backend.dataPath + "/window.conf"
        category: "window"
        property bool staysOnTop: false
        property int windowX: 0
        property int windowY: 0
        property bool havePosition: false

        Component.onCompleted: {
            // Children complete before the window is shown, so this lands before the first frame.
            win.staysOnTop = staysOnTop
            win.restorePosition()
            win.positionReady = true
        }
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
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: backend.menu() }
    Shortcut {
        sequence: "Backspace"
        enabled: !backend.signingIn && (!win.typing || player.searchVisible && player.searchText.length === 0 || player.mixVisible && player.mixText.length === 0)
        context: Qt.ApplicationShortcut
        onActivated: backend.menu()
    }
    Shortcut { sequence: "Return"; enabled: !backend.signingIn; context: Qt.ApplicationShortcut; onActivated: backend.center() }
    Shortcut { sequence: "Enter"; enabled: !backend.signingIn; context: Qt.ApplicationShortcut; onActivated: backend.center() }
    Shortcut { sequence: "Up"; enabled: !backend.signingIn; context: Qt.ApplicationShortcut; onActivated: backend.wheel(-1) }
    Shortcut { sequence: "Down"; enabled: !backend.signingIn; context: Qt.ApplicationShortcut; onActivated: backend.wheel(1) }
    Shortcut { sequence: "Left"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.previousTrack() }
    Shortcut { sequence: "Right"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.nextTrack() }
    Shortcut { sequence: "-"; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.nudgeVolume(-1) }
    Shortcut { sequence: "="; enabled: !backend.signingIn && !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.nudgeVolume(1) }
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
                // Stays full size so the song keeps reporting. Covered once signed in.
                anchors.fill: parent
                anchors.bottomMargin: backend.signingIn ? 72 : 0
                opacity: backend.signingIn ? 1 : 0
                backgroundColor: "#14110c"
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
        interval: 900
        running: backend.shotPath !== ""
        onTriggered: player.grabToImage(function (result) {
            result.saveToFile(backend.shotPath)
            Qt.quit()
        })
    }

    Player {
        id: player
        anchors.fill: parent
        z: 2
        visible: !backend.signingIn
        window: win
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
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
            Text { text: "Click"; color: "white"; font.family: "Inter"; font.pixelSize: 22; font.weight: Font.Bold }
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
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("?   shortcuts") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("T   always on top") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Q   quit") }
        }
    }
}
