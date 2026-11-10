import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtWebEngine
import QtWebChannel
import ClickUi

ApplicationWindow {
    id: win
    width: 320
    height: 548
    minimumWidth: 320
    minimumHeight: 548
    maximumWidth: 320
    maximumHeight: 548
    visible: true
    title: "Click"
    color: "#14110c"
    flags: Qt.Window | Qt.FramelessWindowHint

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool faceOpened: false
    property string authHost: ""

    // The animated face is the home screen. Library stays put after they open it.
    Timer {
        interval: 700
        running: !backend.preview && backend.signedIn && !win.faceOpened
        onTriggered: {
            win.faceOpened = true
            if (backend.kind !== "search")
                backend.openPlayer()
        }
    }

    // While Apple's page is up, these would swallow Enter and the arrow keys.
    Shortcut { sequence: "?"; enabled: !backend.signingIn && backend.kind !== "search"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; enabled: !backend.signingIn && backend.kind !== "search"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; enabled: !backend.signingIn && backend.kind !== "search"; context: Qt.ApplicationShortcut; onActivated: backend.playPause() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: backend.menu() }
    Shortcut {
        sequence: "Backspace"
        enabled: !backend.signingIn && (backend.kind !== "search" || searchBox.visible && search.text.length === 0)
        context: Qt.ApplicationShortcut
        onActivated: backend.menu()
    }
    Shortcut { sequence: "Return"; enabled: !backend.signingIn; context: Qt.ApplicationShortcut; onActivated: backend.center() }
    Shortcut { sequence: "Enter"; enabled: !backend.signingIn; context: Qt.ApplicationShortcut; onActivated: backend.center() }
    Shortcut { sequence: "Up"; enabled: !backend.signingIn; context: Qt.ApplicationShortcut; onActivated: backend.wheel(-1) }
    Shortcut { sequence: "Down"; enabled: !backend.signingIn; context: Qt.ApplicationShortcut; onActivated: backend.wheel(1) }
    Shortcut { sequence: "Left"; enabled: !backend.signingIn && backend.kind !== "search"; context: Qt.ApplicationShortcut; onActivated: backend.previousTrack() }
    Shortcut { sequence: "Right"; enabled: !backend.signingIn && backend.kind !== "search"; context: Qt.ApplicationShortcut; onActivated: backend.nextTrack() }
    Shortcut { sequence: "-"; enabled: !backend.signingIn && backend.kind !== "search"; context: Qt.ApplicationShortcut; onActivated: backend.nudgeVolume(-1) }
    Shortcut { sequence: "="; enabled: !backend.signingIn && backend.kind !== "search"; context: Qt.ApplicationShortcut; onActivated: backend.nudgeVolume(1) }

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
        onTriggered: stage.grabToImage(function (result) {
            result.saveToFile(backend.shotPath)
            Qt.quit()
        })
    }

    Item {
        id: stage
        anchors.fill: parent
        z: 2
        visible: !backend.signingIn
        property real wave: 0
        property var ink: [232, 137, 154]
        property int visMode: 0
        property real tiltX: 0
        property real tiltY: 0
        property var ripples: []
        readonly property color rose: theme.accent
        readonly property color peach: "#ffd8bc"
        readonly property color lake: "#8eb4c0"
        readonly property color cream: "#f8e7dc"
        readonly property color snow: "#fff6ee"
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        function clock(seconds) {
            let whole = Math.max(0, Math.round(seconds))
            const hours = Math.floor(whole / 3600)
            whole = whole % 3600
            const minutes = Math.floor(whole / 60)
            const secs = whole % 60
            const secText = (secs < 10 ? "0" : "") + secs
            if (hours > 0)
                return hours + ":" + (minutes < 10 ? "0" : "") + minutes + ":" + secText
            return minutes + ":" + secText
        }

        NumberAnimation on wave {
            running: true
            from: 0
            to: Math.PI * 2
            duration: backend.playing ? 900 : 1800
            loops: Animation.Infinite
        }
        function decayRipples() {
            if (ripples.length === 0)
                return
            const next = []
            for (let i = 0; i < ripples.length; i++) {
                const drop = ripples[i]
                const alpha = drop.a - 0.03
                if (alpha > 0)
                    next.push({ x: drop.x, y: drop.y, r: drop.r + 3.2, a: alpha })
            }
            ripples = next
        }
        onWaveChanged: {
            decayRipples()
            wash.requestPaint()
            rim.requestPaint()
            if (orb.visible)
                orb.requestPaint()
        }
        onInkChanged: wash.requestPaint()

        Behavior on tiltX { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on tiltY { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

        // Empty margin is a drag handle. The glass title is the other one.
        MouseArea {
            z: -2
            anchors.fill: parent
            onPressed: win.startSystemMove()
        }

        // Opaque pine, so the page underneath cannot show through the glass.
        Rectangle {
            anchors.fill: parent
            color: "#14110c"
            z: -1
        }

        Canvas {
            id: wash
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                const phase = stage.wave
                const ink = stage.ink
                const rose = ink && ink.length >= 3 ? ink : [232, 137, 154]
                const spots = [
                    [width * (0.16 + 0.18 * Math.sin(phase)), height * (0.2 + 0.1 * Math.cos(phase * 0.7)), 220, rose, 0.95],
                    [width * (0.8 + 0.14 * Math.cos(phase * 0.85)), height * (0.76 + 0.12 * Math.sin(phase)), 200, [255, 216, 188], 0.8],
                    [width * (0.5 + 0.2 * Math.sin(phase * 0.55 + 1.2)), height * (0.46 + 0.14 * Math.cos(phase * 0.9)), 180, [142, 180, 192], 0.72]
                ]
                for (let i = 0; i < spots.length; i++) {
                    const spot = spots[i]
                    const rgb = spot[3]
                    const glow = ctx.createRadialGradient(spot[0], spot[1], 10, spot[0], spot[1], spot[2])
                    glow.addColorStop(0, "rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + "," + spot[4] + ")")
                    glow.addColorStop(0.45, "rgba(" + rgb[0] + "," + rgb[1] + "," + rgb[2] + "," + (spot[4] * 0.35) + ")")
                    glow.addColorStop(1, "rgba(20,17,12,0)")
                    ctx.fillStyle = glow
                    ctx.beginPath()
                    ctx.arc(spot[0], spot[1], spot[2], 0, Math.PI * 2)
                    ctx.fill()
                }
            }
        }

        Rectangle {
            id: glass
            anchors.fill: parent
            anchors.margins: 8
            radius: 32
            border.width: 1
            border.color: "#99fff6ee"
            gradient: Gradient {
                GradientStop { position: 0; color: "#28f8e7dc" }
                GradientStop { position: 0.42; color: "#4014110c" }
                GradientStop { position: 1; color: "#c014110c" }
            }
            transform: [
                Rotation {
                    origin.x: glass.width / 2
                    origin.y: glass.height / 2
                    axis { x: 1; y: 0; z: 0 }
                    angle: -stage.tiltY * 0.7
                },
                Rotation {
                    origin.x: glass.width / 2
                    origin.y: glass.height / 2
                    axis { x: 0; y: 1; z: 0 }
                    angle: stage.tiltX * 0.7
                }
            ]

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 1
                height: 36
                radius: 32
                gradient: Gradient {
                    GradientStop { position: 0; color: "#55fff6ee" }
                    GradientStop { position: 1; color: "#00fff6ee" }
                }
            }

            Canvas {
                id: rim
                anchors.fill: parent
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    const r = 32
                    const w = width - 1
                    const h = height - 1
                    ctx.beginPath()
                    ctx.moveTo(r, 1)
                    ctx.arcTo(w, 1, w, h, r)
                    ctx.arcTo(w, h, 1, h, r)
                    ctx.arcTo(1, h, 1, 1, r)
                    ctx.arcTo(1, 1, w, 1, r)
                    ctx.closePath()
                    const sweep = stage.wave
                    const grad = ctx.createLinearGradient(
                        width * (0.5 + 0.5 * Math.cos(sweep)),
                        height * (0.5 + 0.5 * Math.sin(sweep)),
                        width * (0.5 - 0.5 * Math.cos(sweep)),
                        height * (0.5 - 0.5 * Math.sin(sweep)))
                    grad.addColorStop(0, "rgba(255,246,238,0.95)")
                    grad.addColorStop(0.22, "rgba(255,216,188,0.2)")
                    grad.addColorStop(0.5, "rgba(232,137,154,0)")
                    grad.addColorStop(0.78, "rgba(142,180,192,0.75)")
                    grad.addColorStop(1, "rgba(255,246,238,0.35)")
                    ctx.lineWidth = 1.7
                    ctx.strokeStyle = grad
                    ctx.stroke()
                }
            }

            Item {
                id: header
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                height: 40
                z: 2

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    height: 28
                    Rectangle {
                        width: playerLabel.implicitWidth + 18
                        height: 28
                        radius: 14
                        color: backend.kind === "now" ? "#33f8e7dc" : "#14f8e7dc"
                        border.color: backend.kind === "now" ? "#80fff6ee" : "#28fff6ee"
                        border.width: 1
                        Text {
                            id: playerLabel
                            anchors.centerIn: parent
                            text: qsTr("Player")
                            color: stage.snow
                            font.family: "Inter"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: backend.openPlayer()
                        }
                    }
                    Rectangle {
                        width: libraryLabel.implicitWidth + 18
                        height: 28
                        radius: 14
                        color: backend.kind !== "now" ? "#33f8e7dc" : "#14f8e7dc"
                        border.color: backend.kind !== "now" ? "#80fff6ee" : "#28fff6ee"
                        border.width: 1
                        Text {
                            id: libraryLabel
                            anchors.centerIn: parent
                            text: qsTr("Library")
                            color: stage.snow
                            font.family: "Inter"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: if (backend.kind === "now") backend.menu()
                        }
                    }
                }
                Rectangle {
                    id: modeButton
                    visible: backend.kind === "now"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: visible ? modeName.implicitWidth + 16 : 0
                    height: 28
                    radius: 14
                    color: "#24f8e7dc"
                    border.color: "#66fff6ee"
                    border.width: 1
                    Text {
                        id: modeName
                        anchors.centerIn: parent
                        text: stage.visMode === 0 ? qsTr("Orb") : stage.visMode === 1 ? qsTr("Silk") : stage.visMode === 2 ? qsTr("Bars") : qsTr("Disc")
                        color: stage.snow
                        font.family: "Inter"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.4
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: stage.visMode = (stage.visMode + 1) % 4
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    z: -1
                    onPressed: win.startSystemMove()
                }
            }

            HoverHandler {
                id: tilt
                onHoveredChanged: if (!hovered) {
                    stage.tiltX = 0
                    stage.tiltY = 0
                }
                onPointChanged: {
                    const p = tilt.point.position
                    stage.tiltX = (p.x / glass.width - 0.5) * 12
                    stage.tiltY = (p.y / glass.height - 0.5) * 9
                }
            }

            Item {
                id: body
                anchors.top: header.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: dial.top
                anchors.bottomMargin: 4

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    anchors.topMargin: 4
                    visible: backend.kind === "now"
                    spacing: 6

                    Item {
                        width: parent.width
                        height: Math.max(96, parent.height - 118)
                        Image {
                            id: art
                            anchors.fill: parent
                            visible: false
                            source: backend.artwork
                            fillMode: Image.PreserveAspectCrop
                            onStatusChanged: if (status === Image.Ready) sampler.requestPaint()
                        }
                        Canvas {
                            id: sampler
                            width: 8
                            height: 8
                            visible: false
                            onPaint: {
                                if (art.status !== Image.Ready)
                                    return
                                const ctx = getContext("2d")
                                try {
                                    ctx.drawImage(art, 0, 0, 8, 8)
                                    const px = ctx.getImageData(4, 4, 1, 1).data
                                    if (!px || px.length < 3 || px[3] < 40)
                                        return
                                    const dull = px[0] + px[1] + px[2] < 80
                                    stage.ink = dull ? [232, 137, 154] : [px[0], px[1], px[2]]
                                } catch (error) {
                                }
                            }
                        }
                        Canvas {
                            id: orb
                            anchors.fill: parent
                            visible: backend.kind === "now"
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                const phase = stage.wave
                                const playing = backend.playing
                                const energy = playing ? 0.45 + 0.55 * Math.abs(Math.sin(phase * 0.85)) : 0.12
                                function band(i) {
                                    if (!playing)
                                        return 0.12
                                    const a = Math.abs(Math.sin(phase * 1.8 + i * 0.41))
                                    const b = Math.abs(Math.sin(phase * 0.72 + i * 0.23))
                                    return 0.2 + 0.8 * a * (0.3 + 0.7 * b)
                                }
                                function capsule(x, y, w, h) {
                                    const r = Math.min(w / 2, 10)
                                    ctx.beginPath()
                                    ctx.moveTo(x + r, y)
                                    ctx.arcTo(x + w, y, x + w, y + h, r)
                                    ctx.arcTo(x + w, y + h, x, y + h, r)
                                    ctx.arcTo(x, y + h, x, y, r)
                                    ctx.arcTo(x, y, x + w, y, r)
                                    ctx.closePath()
                                    ctx.fill()
                                }
                                const rose = "#e8899a"
                                const peach = "#ffd8bc"
                                const lake = "#8eb4c0"
                                if (stage.visMode === 3) {
                                    const cx = width / 2
                                    const cy = height / 2
                                    const radius = Math.min(width, height) * 0.36
                                    ctx.save()
                                    ctx.translate(cx, cy)
                                    ctx.rotate(playing ? phase * 0.35 : phase * 0.08)
                                    ctx.beginPath()
                                    ctx.arc(0, 0, radius, 0, Math.PI * 2)
                                    const disc = ctx.createRadialGradient(-radius * 0.2, -radius * 0.25, radius * 0.1, 0, 0, radius)
                                    disc.addColorStop(0, "#3a332c")
                                    disc.addColorStop(0.45, "#1a1612")
                                    disc.addColorStop(1, "#0c0a08")
                                    ctx.fillStyle = disc
                                    ctx.fill()
                                    ctx.strokeStyle = "rgba(255,246,238,0.16)"
                                    for (let ring = 8; ring < radius; ring += 5) {
                                        ctx.beginPath()
                                        ctx.arc(0, 0, ring, 0, Math.PI * 2)
                                        ctx.lineWidth = 1
                                        ctx.stroke()
                                    }
                                    ctx.restore()
                                    const medallion = radius * 0.46
                                    ctx.save()
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, medallion, 0, Math.PI * 2)
                                    ctx.clip()
                                    if (art.status === Image.Ready)
                                        ctx.drawImage(art, cx - medallion, cy - medallion, medallion * 2, medallion * 2)
                                    else {
                                        ctx.fillStyle = rose
                                        ctx.fillRect(cx - medallion, cy - medallion, medallion * 2, medallion * 2)
                                    }
                                    ctx.restore()
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, 5, 0, Math.PI * 2)
                                    ctx.fillStyle = "#14110c"
                                    ctx.fill()
                                    ctx.strokeStyle = "rgba(255,246,238,0.7)"
                                    ctx.stroke()
                                    ctx.save()
                                    ctx.translate(cx, cy)
                                    ctx.rotate(playing ? phase * 0.9 : phase * 0.15)
                                    ctx.beginPath()
                                    ctx.arc(0, 0, radius * 0.92, -0.4, 0.55)
                                    ctx.strokeStyle = "rgba(255,246,238,0.28)"
                                    ctx.lineWidth = radius * 0.18
                                    ctx.stroke()
                                    ctx.restore()
                                } else if (stage.visMode === 1) {
                                    const colors = [rose, peach, lake, "#f4c0e0"]
                                    for (let r = 0; r < 4; r++) {
                                        ctx.beginPath()
                                        const y0 = height * (0.18 + r * 0.18)
                                        const amp = 16 + r * 8 + energy * 28
                                        ctx.moveTo(0, y0)
                                        for (let x = 0; x <= width; x += 6) {
                                            const f = band(Math.floor(x / 12))
                                            const y = y0
                                                + Math.sin(x * 0.02 + phase * (1.1 + r * 0.28)) * amp * (0.45 + f)
                                                + Math.cos(x * 0.011 - phase * 0.8) * 10
                                            ctx.lineTo(x, y)
                                        }
                                        ctx.strokeStyle = colors[r]
                                        ctx.globalAlpha = 0.85
                                        ctx.lineWidth = 2.4 + r * 0.7
                                        ctx.stroke()
                                    }
                                    ctx.globalAlpha = 1
                                } else if (stage.visMode === 2) {
                                    const bars = 24
                                    const gap = (width * 0.86) / bars
                                    const start = (width - bars * gap) / 2
                                    for (let i = 0; i < bars; i++) {
                                        const level = band(i)
                                        const barH = Math.max(8, level * height * 0.78)
                                        const x = start + i * gap
                                        const y = height * 0.88 - barH
                                        const grad = ctx.createLinearGradient(0, y, 0, y + barH)
                                        grad.addColorStop(0, "#fff6ee")
                                        grad.addColorStop(0.4, peach)
                                        grad.addColorStop(1, rose)
                                        ctx.fillStyle = grad
                                        capsule(x + 1, y, Math.max(3, gap - 3), barH)
                                    }
                                } else {
                                    const cx = width / 2
                                    const cy = height / 2
                                    const ink = stage.ink && stage.ink.length >= 3 ? stage.ink : [232, 137, 154]
                                    const base = Math.min(width, height) * 0.3 + energy * 16
                                    const count = 64
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, base * (1.28 + energy * 0.16), 0, Math.PI * 2)
                                    ctx.fillStyle = "rgba(" + ink[0] + "," + ink[1] + "," + ink[2] + "," + (0.16 + energy * 0.2) + ")"
                                    ctx.fill()
                                    const pts = []
                                    for (let i = 0; i < count; i++) {
                                        const angle = (i / count) * Math.PI * 2
                                        const freq = band(i % 24)
                                        const wobble = Math.sin(angle * 3 + phase * 2) * base * (0.06 + energy * 0.05)
                                            + Math.cos(angle * 5 - phase * 1.7) * base * (0.04 + freq * 0.05)
                                        const radius = base + wobble
                                        pts.push([cx + Math.cos(angle) * radius, cy + Math.sin(angle) * radius])
                                    }
                                    ctx.beginPath()
                                    const last = pts[count - 1]
                                    ctx.moveTo((pts[0][0] + last[0]) / 2, (pts[0][1] + last[1]) / 2)
                                    for (let i = 0; i < count; i++) {
                                        const next = pts[(i + 1) % count]
                                        ctx.quadraticCurveTo(pts[i][0], pts[i][1], (pts[i][0] + next[0]) / 2, (pts[i][1] + next[1]) / 2)
                                    }
                                    ctx.closePath()
                                    const grad = ctx.createRadialGradient(cx - base * 0.28, cy - base * 0.34, base * 0.08, cx, cy, base * 1.35)
                                    grad.addColorStop(0, "rgba(255,246,238,0.98)")
                                    grad.addColorStop(0.28, "rgba(255,216,188,0.9)")
                                    grad.addColorStop(0.62, "rgba(" + ink[0] + "," + ink[1] + "," + ink[2] + ",0.82)")
                                    grad.addColorStop(1, "rgba(142,180,192,0.15)")
                                    ctx.fillStyle = grad
                                    ctx.shadowColor = "rgba(" + ink[0] + "," + ink[1] + "," + ink[2] + ",0.85)"
                                    ctx.shadowBlur = 26 + energy * 30
                                    ctx.fill()
                                    ctx.shadowBlur = 0
                                    ctx.lineWidth = 1.8
                                    ctx.strokeStyle = "rgba(255,246,238,0.72)"
                                    ctx.stroke()
                                    ctx.save()
                                    ctx.translate(cx - base * 0.22, cy - base * 0.28)
                                    ctx.rotate(-0.6)
                                    ctx.scale(1, 0.55)
                                    ctx.beginPath()
                                    ctx.arc(0, 0, base * 0.28, 0, Math.PI * 2)
                                    ctx.fillStyle = "rgba(255,246,238," + (0.28 + energy * 0.25) + ")"
                                    ctx.fill()
                                    ctx.restore()
                                }
                                for (let i = 0; i < stage.ripples.length; i++) {
                                    const drop = stage.ripples[i]
                                    ctx.beginPath()
                                    ctx.arc(drop.x, drop.y, drop.r, 0, Math.PI * 2)
                                    ctx.strokeStyle = "rgba(255,216,188," + drop.a + ")"
                                    ctx.lineWidth = 2
                                    ctx.stroke()
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: function (mouse) {
                                    const next = stage.ripples.slice()
                                    next.push({ x: mouse.x, y: mouse.y, r: 6, a: 0.9 })
                                    if (next.length > 6)
                                        next.shift()
                                    stage.ripples = next
                                    orb.requestPaint()
                                }
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        text: backend.track !== "" ? backend.track : (backend.playing ? qsTr("Loading…") : qsTr("Nothing playing"))
                        color: stage.snow
                        elide: Text.ElideRight
                        font.family: "Inter Display"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        text: backend.artist + (backend.album !== "" ? "  ·  " + backend.album : "")
                        color: "#a6f8e7dc"
                        elide: Text.ElideRight
                        font.family: "Inter"
                        font.pixelSize: 12
                    }
                    Rectangle {
                        id: progressTrack
                        width: parent.width
                        height: 4
                        radius: 2
                        color: "#24f8e7dc"
                        MouseArea {
                            anchors.fill: parent
                            anchors.topMargin: -10
                            anchors.bottomMargin: -10
                            onClicked: function (mouse) {
                                if (backend.duration > 0)
                                    backend.seek(backend.duration * Math.max(0, Math.min(1, mouse.x / width)))
                            }
                        }
                        Rectangle {
                            id: played
                            width: parent.width * (backend.duration > 0 ? Math.min(1, backend.position / backend.duration) : 0)
                            height: parent.height
                            radius: 2
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "#e8899a" }
                                GradientStop { position: 0.55; color: "#ffd8bc" }
                                GradientStop { position: 1; color: "#8eb4c0" }
                            }
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                x: Math.max(0, parent.width - 8)
                                width: 8
                                height: 8
                                radius: 4
                                color: stage.snow
                                visible: played.width > 6
                            }
                        }
                    }
                    Item {
                        width: parent.width
                        height: 14
                        Text {
                            anchors.left: parent.left
                            text: backend.positionLabel
                            color: "#8cf8e7dc"
                            font.family: "Inter"
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                        Text {
                            anchors.right: parent.right
                            text: "-" + stage.clock(Math.max(0, backend.duration - backend.position))
                            color: "#8cf8e7dc"
                            font.family: "Inter"
                            font.pixelSize: 10
                            font.weight: Font.Medium
                        }
                    }
                }

                Column {
                    anchors.fill: parent
                    visible: backend.kind !== "now"
                    spacing: 0

                    Item {
                        id: searchBox
                        width: parent.width
                        height: backend.kind === "search" ? 36 : 0
                        visible: height > 0
                        onVisibleChanged: if (visible) {
                            search.text = ""
                            search.forceActiveFocus()
                        }
                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            anchors.topMargin: 4
                            radius: 12
                            color: "#1af8e7dc"
                            border.color: "#33fff6ee"
                            border.width: 1
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 22
                            anchors.verticalCenter: parent.verticalCenter
                            visible: search.text.length === 0
                            text: qsTr("Search")
                            color: "#73f8e7dc"
                            font.family: "Inter"
                            font.pixelSize: 13
                        }
                        TextInput {
                            id: search
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            leftPadding: 12
                            rightPadding: 12
                            verticalAlignment: TextInput.AlignVCenter
                            color: stage.snow
                            font.family: "Inter"
                            font.pixelSize: 13
                            clip: true
                            selectByMouse: true
                            onTextChanged: backend.setSearch(text)
                        }
                    }

                    Item {
                        id: nowChip
                        width: parent.width
                        height: backend.kind === "search" ? 0 : 58
                        visible: height > 0

                        Item {
                            anchors.left: parent.left
                            anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28
                            height: 22
                            Repeater {
                                model: 5
                                delegate: Rectangle {
                                    required property int index
                                    x: index * 6
                                    width: 3
                                    radius: 2
                                    color: index % 2 === 0 ? "#e8899a" : "#ffd8bc"
                                    height: 5 + 16 * Math.abs(Math.sin(stage.wave * 1.8 + index * 0.85))
                                    anchors.bottom: parent.bottom
                                }
                            }
                        }
                        Column {
                            anchors.left: parent.left
                            anchors.leftMargin: 48
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Text {
                                width: parent.width
                                text: backend.track !== "" ? backend.track : (backend.playing ? qsTr("Loading…") : qsTr("Nothing playing"))
                                color: stage.snow
                                elide: Text.ElideRight
                                font.family: "Inter Display"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                visible: text !== ""
                                text: backend.artist !== "" ? backend.artist : (backend.playing ? backend.positionLabel : "")
                                color: "#a6f8e7dc"
                                elide: Text.ElideRight
                                font.family: "Inter"
                                font.pixelSize: 11
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: backend.openPlayer()
                        }
                    }

                    ListView {
                        id: songs
                        width: parent.width
                        height: parent.height - searchBox.height - footer.height - nowChip.height
                        clip: true
                        model: backend.rows
                        currentIndex: backend.selected
                        boundsBehavior: Flickable.StopAtBounds
                        highlightMoveDuration: 140
                        highlightResizeDuration: 80
                        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

                        highlight: Item {
                            Rectangle {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                anchors.topMargin: 3
                                anchors.bottomMargin: 3
                                radius: 12
                                color: Qt.rgba(stage.rose.r, stage.rose.g, stage.rose.b, 0.34)
                                border.color: "#66ffd8bc"
                                border.width: 1
                            }
                        }

                        delegate: Item {
                            required property int index
                            required property string label
                            required property string detail
                            width: songs.width
                            height: 36

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 18
                                anchors.right: detail === "" ? cue.left : detailText.left
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: label
                                color: stage.snow
                                elide: Text.ElideRight
                                font.family: "Inter"
                                font.pixelSize: 13
                                font.weight: index === backend.selected ? Font.DemiBold : Font.Normal
                            }
                            Text {
                                id: cue
                                visible: detail === ""
                                anchors.right: parent.right
                                anchors.rightMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: "›"
                                color: index === backend.selected ? stage.snow : "#8cf8e7dc"
                                font.family: "Inter"
                                font.pixelSize: 16
                            }
                            Text {
                                id: detailText
                                visible: detail !== ""
                                anchors.right: parent.right
                                anchors.rightMargin: 18
                                anchors.verticalCenter: parent.verticalCenter
                                text: detail
                                color: index === backend.selected ? "#fff6ee" : "#8cf8e7dc"
                                elide: Text.ElideRight
                                font.family: "Inter"
                                font.pixelSize: 11
                                width: Math.min(implicitWidth, songs.width * 0.42)
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                    }

                    Text {
                        id: footer
                        width: parent.width
                        height: 18
                        horizontalAlignment: Text.AlignHCenter
                        color: "#73f8e7dc"
                        font.family: "Inter"
                        font.pixelSize: 10
                        font.letterSpacing: 0.8
                        text: {
                            if (backend.status !== "")
                                return backend.status
                            if (backend.rows.length > 0)
                                return qsTr("%1 of %2").arg(backend.selected + 1).arg(backend.rows.length)
                            return ""
                        }
                    }
                }
            }

            Wheel {
                id: dial
                z: 2
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: volumeRow.top
                anchors.bottomMargin: 2
                width: 156
                height: 156
                phase: stage.wave
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: false
                onStepped: function (steps) { backend.wheel(steps) }
                onMenuPressed: backend.menu()
                onPreviousPressed: backend.previousTrack()
                onNextPressed: backend.nextTrack()
                onPlayPressed: backend.playPause()
                onCenterPressed: backend.center()
            }

            Row {
                id: volumeRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                anchors.bottomMargin: 12
                height: 18
                spacing: 8

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "−"
                    color: "#73f8e7dc"
                    font.pixelSize: 11
                }
                Item {
                    width: parent.width - 36
                    height: parent.height
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 4
                        radius: 2
                        color: "#24f8e7dc"
                        Rectangle {
                            width: parent.width * backend.volume
                            height: parent.height
                            radius: 2
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "#8eb4c0" }
                                GradientStop { position: 1; color: "#ffd8bc" }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.topMargin: -8
                        anchors.bottomMargin: -8
                        onPressed: apply(mouse.x)
                        onPositionChanged: if (pressed) apply(mouse.x)
                        function apply(x) {
                            backend.setVolume(Math.max(0, Math.min(1, x / width)))
                        }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "+"
                    color: "#c8f8e7dc"
                    font.pixelSize: 11
                }
            }
        }
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
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Wheel or ↑ ↓   move") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Center or Enter   choose") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Menu or Esc   back") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Space   play or pause") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("← →   previous or next") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("− =   volume") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("?   shortcuts") }
            Text { color: "#d7d7dc"; font.family: "Inter"; font.pixelSize: 14; text: qsTr("Q   quit") }
        }
    }
}
