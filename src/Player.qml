import QtQuick
import QtQuick.Effects
import QtQuick.Window

// The whole player screen: artwork-first now playing, with the library
// sliding over it. Everything it needs comes from `backend`.
//
// Layers, back to front:
//   stage     the colour mesh and the three panes (now playing, library, mini)
//   sheets    Sound and the command palette, frosted glass that blurs the stage
//   chrome    window buttons and the toast
Item {
    id: root

    property var window: null
    property bool plusOpen: backend.previewPanel === "plus"
    // True while the new-playlist field in the + menu owns the keyboard.
    readonly property bool plusTyping: plusMenu.typing
    property bool soundOpen: backend.previewPanel === "sound" || backend.previewPanel === "studio"
    property bool lyricsOpen: backend.previewPanel === "lyrics"
    property bool paletteOpen: backend.previewPanel === "palette"
    property bool styleOpen: backend.previewPanel === "styles" || backend.previewPanel === "styles-desktop" || backend.previewPanel === "wrapped"
    // True while the opening animation is on screen (set by the window).
    property bool introActive: false
    property string settingsTab: backend.previewPanel === "wrapped" ? "wrapped" : "general"
    // The Desktops row only exists where it can work; a screenshot can show it anyway (it does nothing).
    readonly property bool showFollow: desktop.available || backend.previewPanel === "styles-desktop"
    // How the time bar moves. Saved by the window; see MainWindow.qml.
    property string visualStyle: "liquid"
    readonly property var visualStyles: [
        { id: "liquid", label: qsTr("Liquid"), hint: qsTr("One flowing wave: kicks ripple out, snares bounce, hats shimmer") },
        { id: "waveform", label: qsTr("Waveform"), hint: qsTr("The song's shape, rippling as it plays") },
        { id: "bounce", label: qsTr("Equalizer"), hint: qsTr("Bars that dance to the beat") },
        { id: "dots", label: qsTr("Dots"), hint: qsTr("Dots tracing the loudness") },
        { id: "minimal", label: qsTr("Minimal"), hint: qsTr("A calm, quiet line") }
    ]
    readonly property bool mini: window ? window.mini : false
    onMiniChanged: {
        if (mini) { soundOpen = false; paletteOpen = false; styleOpen = false; plusOpen = false; lyricsOpen = false }
        // The cover flies between the two layouts only when the big one is on show.
        flyer.fromNow = nowView && !signedOut
    }
    readonly property bool nowView: backend.kind === "now"
    readonly property bool searching: backend.kind === "search"
    readonly property alias searchText: search.text
    readonly property alias mixText: mixInput.text
    readonly property bool mixVisible: backend.kind === "mix"
    readonly property bool searchVisible: searching
    readonly property bool hasTrack: backend.track !== ""
    // 0 to 1: how far the player has stepped back behind an open sheet (scaled to 0.96, blurred a little).
    readonly property bool anySheet: soundOpen || styleOpen || paletteOpen || lyricsOpen
    property real depth: anySheet ? 1 : 0
    Behavior on depth { SpringAnimation { spring: Glass.springPanel; damping: 0.42; epsilon: 0.004 } }
    // The welcome screen owns the window until they sign in.
    readonly property bool signedOut: !backend.signedIn && !backend.signingIn
    // True while a text field in an overlay owns the keyboard.
    readonly property bool overlayTyping: paletteOpen

    readonly property color ink: Glass.ink
    readonly property color mute: Glass.mute
    readonly property color faint: Glass.faint
    readonly property color accent: Glass.accent
    readonly property string trackKey: backend.track + "\u0000" + backend.artist
    onTrackKeyChanged: {
        if (backend.track !== "") meta.reveal()
        mesh.breathe()
    }

    // The Sound screenshot shows a shaped curve rather than a flat line. Preview
    // keeps effects in memory and never starts PipeWire.
    Component.onCompleted: {
        if (backend.previewPanel === "sound") effects.applyPreset("Vocal Pop")
        // CLICK_PREVIEW_PANEL=studio shows the ten-band studio with a preset applied.
        if (backend.previewPanel === "studio") { effects.advanced = true; effects.applyPreset("Club Punch") }
        if (backend.previewPanel === "heart") backend.rateSong(8)
        // CLICK_PREVIEW_PANEL=style-dots (or style-bounce, style-minimal) shows that style.
        if (backend.previewPanel.indexOf("style-") === 0) visualStyle = backend.previewPanel.slice(6)
        // CLICK_PREVIEW_PANEL=discover shows the empty search screen with its mix chips.
        if (backend.previewPanel === "discover") backend.openSearch()
        // CLICK_PREVIEW_PANEL=flight freezes the cover half way between the big player and the mini one.
        if (backend.previewPanel === "flight" && window) { window.mini = true; window.morph = 0.5 }
    }

    HoverHandler { id: pointer }

    // The player listens to its own audio only while that clock runs: a song is
    // playing and the window is showing. Paused or hidden, it listens to nothing.
    Binding { target: audio; property: "wanted"; value: Glass.live }

    // The shared animation clock runs only while a song plays and the window is up.
    Binding {
        target: Glass
        property: "live"
        value: backend.playing && (root.visible || (root.window !== null && root.window.cinema)) && root.window !== null
               && root.window.visibility !== Window.Minimized && root.window.visibility !== Window.Hidden
    }
    readonly property bool hovering: pointer.hovered

    // Text typed in the palette, handed to the field once its screen opens.
    property string pendingSearch: ""
    function startSearch(text) {
        if (root.searching) {
            search.text = text
            return
        }
        pendingSearch = text
        backend.openSearch()
    }
    function startMix(text) {
        backend.openMix()
        mixInput.text = text
        backend.makeMix(text)
    }
    // Deferred: setting the text talks to the backend, which must not happen
    // while the screen change that opened the search is still being announced.
    function applyPendingSearch() {
        search.text = pendingSearch
        pendingSearch = ""
    }
    onSearchingChanged: if (searching) Qt.callLater(applyPendingSearch)

    // A tap on the heart: "I enjoy this" is an 8; tapping a rated song takes the rating back.
    function toggleHeart() {
        if (!backend.useTaste || !root.hasTrack)
            return
        backend.rateSong(backend.songRating > 0 ? 0 : 8)
    }

    function buildCommands() {
        const list = [
            { id: "playpause", label: backend.playing ? qsTr("Pause") : qsTr("Play"), hint: "Space", keywords: "play pause toggle" },
            { id: "next", label: qsTr("Next song"), hint: "→", keywords: "skip forward" },
            { id: "previous", label: qsTr("Previous song"), hint: "←", keywords: "back" },
            { id: "mute", label: backend.muted ? qsTr("Unmute") : qsTr("Mute"), hint: "", keywords: "volume silence quiet" },
            { id: "search", label: qsTr("Search"), hint: "", keywords: "find song album playlist catalog" },
            { id: "queue", label: qsTr("Up Next"), hint: "U", keywords: "queue" },
            { id: "library", label: qsTr("Library"), hint: "", keywords: "menu music playlists albums artists songs" },
            { id: "shuffle", label: backend.shuffle ? qsTr("Shuffle off") : qsTr("Shuffle on"), hint: "", keywords: "random" },
            { id: "repeat", label: backend.repeatMode === 0 ? qsTr("Repeat all") : backend.repeatMode === 2 ? qsTr("Repeat this song") : qsTr("Repeat off"), hint: "", keywords: "loop" },
            { id: "sound", label: qsTr("Sound"), hint: "", keywords: "eq equalizer bass mid treble reverb effects" },
            { id: "lyrics", label: qsTr("Lyrics"), hint: "", keywords: "words sing along karaoke text" },
            { id: "cinema", label: qsTr("Full screen"), hint: "F11", keywords: "cinema fullscreen big lyrics theater" },
            { id: "styles", label: qsTr("Animation style"), hint: "", keywords: "visualizer visualiser waveform animation look options settings" }
        ]
        if (backend.useTaste && root.hasTrack) {
            list.push({ id: "heart", label: backend.songRating > 0 ? qsTr("Remove my rating") : qsTr("Heart this song"), hint: "H", keywords: "like love favourite rate" })
            list.push({ id: "love", label: qsTr("Love this song (10/10)"), hint: "", keywords: "heart favourite best rate ten" })
        }
        if (root.showFollow)
            list.push({ id: "follow", label: desktop.follow ? qsTr("Stop following me across desktops") : qsTr("Follow me across desktops"), hint: "", keywords: "workspace desktop mini corner carry" })
        list.push({ id: "taste", label: backend.useTaste ? qsTr("Stop learning my taste") : qsTr("Learn my taste"), hint: "", keywords: "hearts memory learn grok setting" })
        if (backend.similarAvailable)
            list.push({ id: "similar", label: backend.autoSimilar ? qsTr("Stop playing similar songs") : qsTr("Play similar songs after this one"),
                        hint: "Grok", keywords: "autoplay auto radio similar next grok setting" })
        for (const style of root.visualStyles)
            list.push({ id: "style", label: qsTr("Animation: %1").arg(style.label), hint: "", keywords: "visualizer waveform style", arg: style.id })
        if (backend.mixAvailable)
            list.push({ id: "openmix", label: qsTr("Make a mix"), hint: "M", keywords: "grok mood playlist" })
        if (effects.available) {
            list.push({ id: "bassboost", label: qsTr("Bass Boost"), hint: "", keywords: "eq sound low" })
            for (const name of effects.presets)
                list.push({ id: "preset", label: qsTr("Sound: %1").arg(name), hint: "", keywords: "eq preset", arg: name })
        }
        if (backend.mixAvailable)
            for (const mood of backend.mixChips)
                list.push({ id: "mix", label: qsTr("Mix: %1").arg(mood), hint: "Grok", keywords: "mood now", arg: mood })
        list.push({ id: "mini", label: qsTr("Mini player"), hint: "Ctrl+M", keywords: "compact small island" })
        list.push({ id: "top", label: root.window && root.window.staysOnTop ? qsTr("Stop staying on top") : qsTr("Always on top"), hint: "T", keywords: "pin float" })
        list.push({ id: "help", label: qsTr("Shortcuts"), hint: "?", keywords: "keys help" })
        list.push({ id: "quit", label: qsTr("Quit"), hint: "Q", keywords: "exit close" })
        return list
    }

    function runCommand(id, arg) {
        root.paletteOpen = false
        if (id === "playpause") backend.playPause()
        else if (id === "next") backend.nextTrack()
        else if (id === "previous") backend.previousTrack()
        else if (id === "mute") backend.toggleMute()
        else if (id === "search") root.startSearch(arg)
        else if (id === "queue") backend.openQueue()
        else if (id === "library") { if (root.nowView) backend.menu() }
        else if (id === "shuffle") backend.setShuffle(!backend.shuffle)
        else if (id === "repeat") backend.cycleRepeat()
        else if (id === "sound") root.soundOpen = true
        else if (id === "styles") root.styleOpen = true
        else if (id === "lyrics") { root.soundOpen = false; root.styleOpen = false; root.lyricsOpen = true }
        else if (id === "cinema") { if (root.window) root.window.setCinema(true) }
        else if (id === "similar") backend.autoSimilar = !backend.autoSimilar
        else if (id === "heart") root.toggleHeart()
        else if (id === "love") backend.rateSong(10)
        else if (id === "taste") backend.useTaste = !backend.useTaste
        else if (id === "follow") desktop.follow = !desktop.follow
        else if (id === "style") root.visualStyle = arg
        else if (id === "openmix") backend.openMix()
        else if (id === "mix") root.startMix(arg)
        else if (id === "bassboost") { effects.bass = 0.7; effects.enabled = true }
        else if (id === "preset") { effects.applyPreset(arg); if (arg !== "Flat Studio") effects.enabled = true }
        else if (id === "vibe") {
            const sound = JSON.parse(arg)
            effects.applyShape(sound.bands, sound.bass, sound.mid, sound.treble, sound.reverb)
            if (sound.summary !== "flat") effects.enabled = true
        }
        else if (id === "mini") { if (root.window) root.window.mini = true }
        else if (id === "top") { if (root.window) root.window.staysOnTop = !root.window.staysOnTop }
        else if (id === "help") { if (root.window) root.window.helpVisible = true }
        else if (id === "quit") Qt.quit()
    }

    // A pill of glass with a label: chips and small buttons.
    component Chip: Rectangle {
        id: chip
        property string label: ""
        property bool strong: false     // filled with ink, for the main action
        property bool tinted: false     // a touch of the accent, for chips made for this moment
        property bool dimmed: false
        property real pad: 14
        signal clicked()
        height: 34
        width: chipText.implicitWidth + pad * 2
        radius: height / 2
        color: strong ? Glass.ink
            : tinted ? Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, chipArea.containsMouse ? 0.42 : 0.26)
            : (chipArea.containsMouse ? Glass.fillHover : Glass.fill)
        border.color: strong ? "transparent" : Glass.specular
        border.width: 1
        opacity: dimmed ? 0.45 : 1
        scale: chipArea.pressed ? 0.94 : 1
        Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
        Behavior on color { ColorAnimation { duration: 140 } }
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.label
            color: chip.strong ? Glass.inkText : Glass.ink
            font.family: Glass.font
            font.pixelSize: 13
            font.weight: chip.strong ? Font.Bold : Font.Normal
        }
        MouseArea {
            id: chipArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: !chip.dimmed
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }

    // Artwork with rounded corners and a gradient stand-in while it loads.
    component Cover: Item {
        id: art
        property real radius: 12
        // A fixed texture size, for a cover that changes size every frame (no reload per frame).
        property real texSize: 0
        Rectangle {
            anchors.fill: parent
            radius: art.radius
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.lighter(Glass.accent, 1.15) }
                GradientStop { position: 1; color: Qt.darker(Glass.accent, 2.4) }
            }
        }
        Image {
            anchors.fill: parent
            source: backend.artwork
            fillMode: Image.PreserveAspectCrop
            asynchronous: !backend.preview
            sourceSize.width: art.texSize > 0 ? art.texSize : Math.ceil(art.width * 2)
            sourceSize.height: art.texSize > 0 ? art.texSize : Math.ceil(art.height * 2)
            layer.enabled: true
            layer.effect: MultiEffect { maskEnabled: true; maskSource: artMask }
        }
        Rectangle { id: artMask; anchors.fill: parent; radius: art.radius; visible: false; layer.enabled: true }
        Rectangle {
            anchors.fill: parent
            radius: art.radius
            color: "transparent"
            border.color: Glass.specular
            border.width: 1
        }
    }

    // ================= STAGE: everything the glass sheets blur =================
    // Behind everything: the dark the player steps back into.
    Rectangle {
        anchors.fill: parent
        color: Glass.base
    }
    // What the sheets float over. When one opens, this steps back like a card: a little smaller (0.96),
    // rounded, and 8 px more blurred, so the sheet reads as nearer. The glass of the sheets samples `stage`
    // inside, which stays sharp, so nothing is blurred twice.
    Rectangle {
        id: stageMask
        anchors.fill: parent
        radius: 24
        visible: false
        layer.enabled: true
    }
    Item {
        id: stageView
        anchors.fill: parent
        scale: 1 - 0.04 * root.depth
        layer.enabled: root.depth > 0.01
        layer.effect: MultiEffect {
            blurEnabled: true
            blurMax: 32
            blur: 0.25 * root.depth
            autoPaddingEnabled: false
            maskEnabled: true
            maskSource: stageMask
        }
    Item {
        id: stage
        anchors.fill: parent

        MeshBackground {
            id: mesh
            anchors.fill: parent
            // It drifts behind the player and the mini player. Behind lists it holds still.
            running: backend.playing && (root.nowView || root.mini)
            // Full colour on the player, calmer behind lists so the words stay easy to read.
            calm: root.nowView || root.mini || root.signedOut ? 0 : 1
        }

        // Drag the frameless window by any empty background.
        MouseArea {
            anchors.fill: parent
            onPressed: if (root.window) root.window.startSystemMove()
        }

        // ================= NOW PLAYING =================
        Item {
            id: nowPane
            anchors.fill: parent
            opacity: root.nowView && !root.mini && !root.signedOut ? 1 : 0
            scale: root.nowView ? 1 : 0.965
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
            Behavior on scale { SpringAnimation { spring: Glass.springPanel; damping: Glass.dampingPanel; epsilon: 0.001 } }

            readonly property real pad: 24
            readonly property real artSize: Math.min(width - pad * 2, height - 358)
            // The cover breathes with the bass, 1.00 to 1.03, through a spring so it swells rather than twitches.
            property real pump: Glass.reactive ? 0.03 * Glass.cover : 0
            Behavior on pump { SpringAnimation { spring: 9; damping: 0.38; epsilon: 0.0005 } }

            // top bar
            IconButton {
                x: 12
                y: 12
                path: "M4 6h16v2H4zM4 11h16v2H4zM4 16h10v2H4z"
                size: 20
                hit: 40
                onClicked: backend.menu()
            }
            IconButton {
                x: 52
                y: 12
                path: "M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"
                size: 20
                hit: 40
                onClicked: backend.openSearch()
            }
            IconButton {
                id: mixButton
                x: 92
                y: 12
                visible: backend.mixAvailable
                path: "M12 2l2.4 7.6L22 12l-7.6 2.4L12 22l-2.4-7.6L2 12l7.6-2.4z"
                size: 18
                hit: 40
                onClicked: backend.openMix()
            }
            // artwork
            Item {
                id: artFrame
                opacity: flyer.flying ? 0 : 1
                width: nowPane.artSize
                height: nowPane.artSize
                x: (parent.width - width) / 2
                y: 60
                scale: backend.playing ? 1 : 0.9
                Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.2; epsilon: 0.001 } }
                transform: Scale {
                    origin.x: artFrame.width / 2
                    origin.y: artFrame.height / 2
                    xScale: 1 + nowPane.pump
                    yScale: 1 + nowPane.pump
                }

                // A ring leaves the cover on every kick, one after the other.
                Repeater {
                    id: artRings
                    property int next: 0
                    model: 2
                    Rectangle {
                        id: ring
                        required property int index
                        anchors.centerIn: parent
                        width: parent.width
                        height: parent.height
                        radius: 24
                        color: "transparent"
                        border.color: Glass.accentLight
                        border.width: 2
                        opacity: 0
                        function fire(strength) {
                            ringAnim.strength = strength
                            ringAnim.restart()
                        }
                        ParallelAnimation {
                            id: ringAnim
                            property real strength: 1
                            NumberAnimation { target: ring; property: "scale"; from: 1.0; to: 1.2 + 0.14 * ringAnim.strength; duration: 560; easing.type: Easing.OutCubic }
                            NumberAnimation { target: ring; property: "opacity"; from: 0.55 * ringAnim.strength; to: 0; duration: 560; easing.type: Easing.OutQuad }
                        }
                    }
                }
                Connections {
                    target: audio
                    enabled: root.nowView && !root.mini
                    // A ring only on a clear hit: a strong kick, or in a bass-free passage any clear sound.
                    function onCoverHit(strength) {
                        artRings.itemAt(artRings.next % 2).fire(strength)
                        artRings.next += 1
                        if (strength > 0.8)
                            fringeAnim.restart()
                    }
                }

                // A halo of the cover's colour behind it that flares with the bass. The blur
                // is drawn once into a texture; only its size and brightness change per frame.
                Item {
                    id: artGlow
                    anchors.centerIn: parent
                    width: parent.width * 0.94
                    height: parent.height * 0.94
                    opacity: !backend.playing ? 0.1
                        : Glass.reactive ? Math.min(1, 0.2 + 0.7 * Glass.cover) : 0.32
                    scale: 1 + (Glass.reactive ? 0.12 * Glass.cover : 0.03)
                    Rectangle {
                        anchors.fill: parent
                        radius: 56
                        color: Glass.accent
                        layer.enabled: true
                        layer.effect: MultiEffect {
                            blurEnabled: true
                            blurMax: 64
                            blur: 1.0
                            autoPaddingEnabled: true
                        }
                    }
                }

                Item {
                    id: artShadowSource
                    anchors.fill: parent
                    Rectangle {
                        anchors.fill: parent
                        radius: 22
                        color: "black"
                    }
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: "#000000"
                        shadowOpacity: 0.5
                        shadowBlur: 1.0
                        shadowVerticalOffset: 14
                        shadowScale: 0.96
                        autoPaddingEnabled: true
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.lighter(root.accent, 1.15) }
                        GradientStop { position: 1; color: Qt.darker(root.accent, 2.4) }
                    }
                    Glyph {
                        x: (parent.width - 64) / 2
                        y: (parent.height - 64) / 2
                        size: 64
                        color: "#66ffffff"
                        path: "M12 3v10.55A4 4 0 1 0 14 17V7h4V3h-6z"
                    }
                }
                Image {
                    id: cover
                    anchors.fill: parent
                    source: backend.artwork
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: !backend.preview
                    smooth: true
                    opacity: status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 450; easing.type: Easing.InOutQuad } }
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: coverMask
                    }
                }
                Rectangle {
                    id: coverMask
                    anchors.fill: parent
                    radius: 22
                    visible: false
                    layer.enabled: true
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 22
                    color: "transparent"
                    border.color: Glass.specular
                    border.width: 1
                }
                // A prismatic fringe on a big hit: red and cyan edges split apart, then fade over 400 ms.
                Item {
                    id: fringe
                    anchors.fill: parent
                    property real split: 0
                    visible: split > 0.01
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -1
                        radius: 23
                        color: "transparent"
                        border.width: 2
                        border.color: "#ff3b5c"
                        opacity: fringe.split * 0.7
                        transform: Translate { x: -3 * fringe.split; y: -1 * fringe.split }
                    }
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -1
                        radius: 23
                        color: "transparent"
                        border.width: 2
                        border.color: "#3be8ff"
                        opacity: fringe.split * 0.7
                        transform: Translate { x: 3 * fringe.split; y: 1 * fringe.split }
                    }
                    NumberAnimation {
                        id: fringeAnim
                        target: fringe; property: "split"; from: 1; to: 0
                        duration: 400; easing.type: Easing.OutCubic
                    }
                }
            }

            // title block: the song once, the artist once
            Column {
                id: meta
                function reveal() { revealAnim.restart() }
                property real shift: 0
                transform: Translate { y: meta.shift }
                ParallelAnimation {
                    id: revealAnim
                    NumberAnimation { target: meta; property: "opacity"; from: 0; to: 1; duration: 420; easing.type: Easing.OutCubic }
                    NumberAnimation { target: meta; property: "shift"; from: 12; to: 0; duration: 520; easing.type: Easing.OutBack; easing.overshoot: 1.6 }
                }
                x: nowPane.pad
                y: artFrame.y + artFrame.height + 22
                // leaves room for the heart on the right
                width: parent.width - nowPane.pad * 2 - (backend.useTaste && root.hasTrack ? 46 : 0)
                spacing: 4
                Text {
                    width: parent.width
                    text: root.hasTrack ? backend.track : (backend.playing ? qsTr("Loading…") : qsTr("Nothing playing"))
                    color: root.ink
                    elide: Text.ElideRight
                    font.family: Glass.font
                    font.pixelSize: 21
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: root.hasTrack ? backend.artist + (backend.album !== "" ? "  ·  " + backend.album : "")
                        : (backend.mixAvailable ? qsTr("Search for a song, or press M for a mix from Grok.") : qsTr("Search for a song to start."))
                    color: root.mute
                    elide: Text.ElideRight
                    font.family: Glass.font
                    font.pixelSize: 13
                    visible: text !== ""
                }
            }

            // The heart. A tap says "I enjoy this" (8 out of 10); hover it for a scale from 1 to 10.
            // Not hearting a song means nothing: it may simply be one you enjoy less than the others.
            Item {
                id: heartBox
                visible: backend.useTaste && root.hasTrack
                z: 5
                readonly property bool open: heartHover.hovered || holdOpen.running || backend.previewPanel === "heart"
                readonly property int rating: backend.songRating
                width: open ? 268 : 44
                height: open ? 104 : 44
                x: parent.width - nowPane.pad - width + 8
                y: meta.y - 6 - (open ? 60 : 0)
                // Stays open a moment after a pick, so the number it landed on can be seen.
                Timer { id: holdOpen; interval: 650 }
                Timer { id: leaveDelay; interval: 220 }
                HoverHandler { id: heartHover }

                // the scale: ten dots, filled up to the hovered one (or the saved rating)
                Item {
                    id: scalePane
                    width: 268
                    height: 54
                    opacity: heartBox.open ? 1 : 0
                    scale: heartBox.open ? 1 : 0.7
                    transformOrigin: Item.BottomRight
                    visible: opacity > 0.01
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                    Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
                    property int preview: 0
                    readonly property int shown: preview > 0 ? preview : heartBox.rating
                    Rectangle {
                        anchors.fill: parent
                        radius: 27
                        color: "#d9181310"
                        border.color: Glass.specular
                        border.width: 1
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: 27
                        gradient: Gradient {
                            GradientStop { position: 0; color: Glass.sheen }
                            GradientStop { position: 0.5; color: "transparent" }
                        }
                    }
                    Row {
                        x: 14
                        y: 0
                        height: parent.height
                        Repeater {
                            model: 10
                            Item {
                                id: slot
                                required property int index
                                readonly property int value: index + 1
                                readonly property bool lit: value <= scalePane.shown
                                width: 24
                                height: scalePane.height
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 12
                                    width: 18
                                    height: 18
                                    radius: 9
                                    color: slot.lit ? Glass.accent : "#33ffffff"
                                    scale: slot.value === scalePane.preview ? 1.3 : 1
                                    Behavior on color { ColorAnimation { duration: 90 } }
                                    Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 33
                                    text: slot.value
                                    color: slot.lit ? Glass.ink : Glass.faint
                                    font.family: Glass.font
                                    font.pixelSize: 11
                                    font.weight: slot.value === scalePane.preview ? Font.Bold : Font.Normal
                                    font.features: { "tnum": 1 }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onEntered: scalePane.preview = slot.value
                                    onExited: if (scalePane.preview === slot.value) scalePane.preview = 0
                                    onClicked: {
                                        backend.rateSong(slot.value)
                                        scalePane.preview = 0
                                        holdOpen.restart()
                                    }
                                }
                            }
                        }
                    }
                }

                // the heart itself, at the bottom right; shows the number once there is one
                Text {
                    visible: heartBox.rating > 0
                    anchors.verticalCenter: heartButton.verticalCenter
                    anchors.right: heartButton.left
                    anchors.rightMargin: -4
                    text: heartBox.rating
                    color: Glass.accentLight
                    font.family: Glass.font
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    font.features: { "tnum": 1 }
                }
                IconButton {
                    id: heartButton
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    hit: 44
                    size: 24
                    tint: heartBox.rating > 0 ? Glass.accent : Glass.mute
                    path: heartBox.rating > 0
                        ? "M12 21.35l-1.45-1.32C5.4 15.36 2 12.28 2 8.5 2 5.42 4.42 3 7.5 3c1.74 0 3.41.81 4.5 2.09C13.09 3.81 14.76 3 16.5 3 19.58 3 22 5.42 22 8.5c0 3.78-3.4 6.86-8.55 11.54L12 21.35z"
                        : "M16.5 3c-1.74 0-3.41.81-4.5 2.09C10.91 3.81 9.24 3 7.5 3 4.42 3 2 5.42 2 8.5c0 3.78 3.4 6.86 8.55 11.54L12 21.35l1.45-1.32C18.6 15.36 22 12.28 22 8.5 22 5.42 19.58 3 16.5 3zm-4.4 15.55l-.1.1-.1-.1C7.14 14.24 4 11.39 4 8.5 4 6.5 5.5 5 7.5 5c1.54 0 3.04.99 3.57 2.36h1.87C13.46 5.99 14.96 5 16.5 5c2 0 3.5 1.5 3.5 3.5 0 2.89-3.14 5.74-7.9 10.05z"
                    onClicked: root.toggleHeart()
                }
            }

            // the waveform time bar
            LiquidScrubber {
                id: scrub
                x: nowPane.pad
                // Leaves room above the wave for the time capsule, clear of the artist line.
                y: meta.y + 46
                width: parent.width - nowPane.pad * 2
                position: backend.position
                duration: backend.duration
                playing: backend.playing
                trackKey: root.trackKey
                style: root.visualStyle
                pin: backend.previewPanel === "scrub" ? 0.62 : -1
                onSeekRequested: function (seconds) { backend.seek(seconds) }
            }

            // transport, with shuffle and repeat at the ends
            Row {
                id: transport
                anchors.horizontalCenter: parent.horizontalCenter
                y: scrub.y + scrub.height + 4
                spacing: 10
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    path: "M10.59 9.17L5.41 4 4 5.41l5.17 5.17 1.42-1.41zM14.5 4l2.04 2.04L4 18.59 5.41 20 17.96 7.46 20 9.5V4h-5.5zm.33 9.41l-1.41 1.41 3.13 3.13L14.5 20H20v-5.5l-2.04 2.04-3.13-3.13z"
                    size: 20
                    hit: 40
                    tint: backend.shuffle ? Glass.accentLight : root.mute
                    marked: backend.shuffle
                    onClicked: backend.setShuffle(!backend.shuffle)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    path: "M6 6h2v12H6zM9.5 12l8.5 6V6z"
                    size: 26
                    hit: 52
                    onClicked: backend.previousTrack()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    playState: backend.playing ? 1 : 0
                    size: 30
                    hit: 64
                    filled: true
                    fill: root.ink
                    tint: Glass.inkText
                    onClicked: backend.playPause()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    path: "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"
                    size: 26
                    hit: 52
                    onClicked: backend.nextTrack()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    // repeat-one draws a small 1 inside the loop
                    path: backend.repeatMode === 1
                        ? "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4zm-4-2V9h-1l-2 1v1h1.5v4H13z"
                        : "M7 7h10v3l4-4-4-4v3H5v6h2V7zm10 10H7v-3l-4 4 4 4v-3h12v-6h-2v4z"
                    size: 20
                    hit: 40
                    tint: backend.repeatMode !== 0 ? Glass.accentLight : root.mute
                    marked: backend.repeatMode !== 0
                    onClicked: backend.cycleRepeat()
                }
            }

            // Full screen: the big cover and the synced lyrics (F11).
            IconButton {
                x: 14
                y: parent.height - 48
                hit: 44
                size: 22
                path: "M5 5h5v2H7v3H5V5zm9 0h5v5h-2V7h-3V5zM5 14h2v3h3v2H5v-5zm12 0h2v5h-5v-2h3v-3z"
                onClicked: if (root.window) root.window.setCinema(true)
            }
            IconButton {
                x: parent.width - width - 62
                y: parent.height - 48
                hit: 44
                size: 22
                tint: effects.enabled ? Glass.accentLight : root.ink
                path: "M3 17v2h6v-2H3zM3 5v2h10V5H3zm10 14v2h8v-2h-8zM7 9v2H3v2h4v2h2V9H7zm14 4v-2H11v2h10zm-6-4h2V7h4V5h-4V3h-2v6z"
                onClicked: root.soundOpen = true
            }
            IconButton {
                x: parent.width - width - 14
                y: parent.height - 48
                hit: 44
                size: 22
                path: "M3 13h2v-2H3v2zm0 4h2v-2H3v2zm0-8h2V7H3v2zm4 4h14v-2H7v2zm0 4h14v-2H7v2zM7 7v2h14V7H7z"
                onClicked: backend.openQueue()
            }
        }

        // ================= LIBRARY =================
        Item {
            id: libraryPane
            width: parent.width
            height: parent.height
            x: root.nowView ? width * 0.35 : 0
            opacity: root.nowView || root.mini || root.signedOut ? 0 : 1
            visible: opacity > 0.01
            Behavior on x { SpringAnimation { spring: Glass.springPanel; damping: 0.34; epsilon: 0.1 } }
            Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

            IconButton {
                id: back
                x: 12
                y: 12
                path: "M15.41 7.41L14 6l-6 6 6 6 1.41-1.41L10.83 12z"
                size: 24
                hit: 40
                onClicked: backend.back()
            }
            Text {
                anchors.left: back.right
                anchors.right: parent.right
                anchors.rightMargin: 112
                y: 12
                height: 40
                verticalAlignment: Text.AlignVCenter
                text: backend.title
                color: root.ink
                elide: Text.ElideRight
                font.family: Glass.font
                font.pixelSize: 20
                font.weight: Font.Bold
            }

            // search field, only on the search screen
            Item {
                id: searchBox
                x: 16
                y: 60
                width: parent.width - 32
                height: root.searching ? 40 : 0
                visible: height > 1
                clip: true
                Behavior on height { SpringAnimation { spring: Glass.springPanel; damping: 0.4; epsilon: 0.1 } }
                onVisibleChanged: if (visible) search.forceActiveFocus()
                Rectangle {
                    anchors.fill: parent
                    radius: 14
                    color: Glass.fill
                    border.color: search.activeFocus ? "#80ffffff" : Glass.specular
                    border.width: 1
                }
                Glyph {
                    x: 12
                    y: 11
                    size: 18
                    color: root.faint
                    path: "M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"
                }
                Text {
                    x: 40
                    anchors.verticalCenter: parent.verticalCenter
                    visible: search.text.length === 0
                    text: qsTr("Artists, songs, albums")
                    color: root.faint
                    font.family: Glass.font
                    font.pixelSize: 14
                }
                TextInput {
                    id: search
                    x: 40
                    width: parent.width - 52
                    height: parent.height
                    verticalAlignment: TextInput.AlignVCenter
                    color: root.ink
                    font.family: Glass.font
                    font.pixelSize: 14
                    clip: true
                    selectByMouse: true
                    onTextChanged: backend.setSearch(text)
                }
            }

            // Before anything is typed: what was searched for last, newest first.
            Column {
                id: discover
                x: 16
                y: searchBox.y + searchBox.height + 8
                width: parent.width - 32
                readonly property bool shown: root.searching && search.text.length === 0 && backend.searchHistory.length > 0
                visible: shown
                height: shown ? implicitHeight : 0
                Item {
                    width: parent.width
                    height: 30
                    Text {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Recent searches")
                        color: Glass.faint
                        font.family: Glass.font
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.8
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Clear")
                        color: clearTap.containsMouse ? Glass.ink : Glass.mute
                        font.family: Glass.font
                        font.pixelSize: 12
                        MouseArea { id: clearTap; anchors.fill: parent; anchors.margins: -6; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: backend.forgetSearch() }
                    }
                }
                Repeater {
                    model: discover.shown ? backend.searchHistory : []
                    Rectangle {
                        id: hrow
                        required property string modelData
                        width: parent.width
                        height: 40
                        radius: 12
                        color: htap.containsMouse ? Glass.fill : "transparent"
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Glyph {
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            size: 16
                            color: Glass.faint
                            path: "M13 3a9 9 0 0 0-9 9H1l3.89 3.89.07.14L9 12H6a7 7 0 1 1 2.05 4.95l-1.42 1.42A9 9 0 1 0 13 3zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z"
                        }
                        Text {
                            x: 38
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 80
                            text: hrow.modelData
                            elide: Text.ElideRight
                            color: root.ink
                            font.family: Glass.font
                            font.pixelSize: 14
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: "×"
                            color: xTap.containsMouse ? Glass.ink : Glass.faint
                            font.pixelSize: 16
                            MouseArea { id: xTap; anchors.fill: parent; anchors.margins: -8; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: backend.forgetSearch(hrow.modelData) }
                        }
                        MouseArea {
                            id: htap
                            anchors.fill: parent
                            z: -1
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: search.text = hrow.modelData
                        }
                    }
                }
            }

            ListView {
                id: list
                x: 8
                y: searchBox.y + searchBox.height + (root.searching ? 8 : 0) + (discover.shown ? discover.height + 8 : 0)
                width: parent.width - 16
                height: parent.height - y - (miniBar.visible ? miniBar.height + 24 : 12)
                clip: true
                visible: backend.kind !== "mix"
                model: backend.rows
                currentIndex: backend.selected
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                highlightFollowsCurrentItem: true
                highlightMoveDuration: 180
                highlightMoveVelocity: -1
                preferredHighlightBegin: height * 0.25
                preferredHighlightEnd: height * 0.75
                highlightRangeMode: ListView.ApplyRange
                onCountChanged: positionViewAtBeginning()

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function (event) { backend.wheel(event.angleDelta.y < 0 ? 1 : -1) }
                }

                highlight: Rectangle {
                    radius: 14
                    color: Glass.fillHover
                    border.color: Glass.specular
                    border.width: 1
                }
                // One soft glow that glides to the row under the pointer, instead of each row lighting up on its own.
                property int hoveredRow: -1
                Rectangle {
                    parent: list.contentItem
                    z: -1
                    x: 0
                    width: list.width
                    height: 54
                    radius: 14
                    color: "#18ffffff"
                    border.color: Glass.rimMiddle
                    border.width: 1
                    readonly property bool shown: list.hoveredRow >= 0 && list.hoveredRow !== backend.selected
                    y: Math.max(0, list.hoveredRow) * (54 + list.spacing)
                    opacity: shown ? 1 : 0
                    Behavior on y { SpringAnimation { spring: Glass.springPanel; damping: 0.4; epsilon: 0.1 } }
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                }

                delegate: Item {
                    id: row
                    required property int index
                    required property var modelData
                    width: list.width
                    height: 54
                    readonly property bool current: index === backend.selected
                    readonly property bool queueable: modelData.action === "play"
                    readonly property bool lit: hover.hovered || current
                    HoverHandler {
                        id: hover
                        onHoveredChanged: {
                            if (hovered)
                                list.hoveredRow = row.index
                            else if (list.hoveredRow === row.index)
                                list.hoveredRow = -1
                        }
                    }
                    // The row floats up a pixel under the pointer.
                    transform: Translate {
                        y: hover.hovered ? -1 : 0
                        Behavior on y { SpringAnimation { spring: Glass.springSnap; damping: 0.5; epsilon: 0.01 } }
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: row.queueable ? 84 : 40
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        Text {
                            width: parent.width
                            text: row.modelData.label
                            color: root.ink
                            elide: Text.ElideRight
                            font.family: Glass.font
                            font.pixelSize: 15
                            font.weight: row.current ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            width: parent.width
                            visible: text !== ""
                            text: row.modelData.detail
                            color: root.mute
                            elide: Text.ElideRight
                            font.family: Glass.font
                            font.pixelSize: 12
                        }
                    }
                    Glyph {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        y: (parent.height - 18) / 2
                        size: 18
                        color: root.faint
                        visible: !row.queueable
                        opacity: row.current ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 160 } }
                        path: "M8.59 16.59L13.17 12 8.59 7.41 10 6l6 6-6 6z"
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            backend.wheel(row.index - backend.selected)
                            backend.center()
                        }
                    }
                    // Play next and add to queue slide in from the right edge, one after the other, each on a spring.
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        visible: row.queueable && (row.lit || first.opacity > 0.01)
                        IconButton {
                            id: first
                            path: "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"
                            size: 18
                            hit: 38
                            opacity: row.lit ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 110 } }
                            transform: Translate {
                                x: row.lit ? 0 : 16
                                Behavior on x { SpringAnimation { spring: Glass.springSnap; damping: 0.36; epsilon: 0.05 } }
                            }
                            onClicked: backend.addToQueue(row.index, true)
                        }
                        IconButton {
                            path: "M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"
                            size: 20
                            hit: 38
                            opacity: row.lit ? 1 : 0
                            Behavior on opacity { SequentialAnimation { PauseAnimation { duration: 45 } NumberAnimation { duration: 110 } } }
                            transform: Translate {
                                x: row.lit ? 0 : 16
                                Behavior on x {
                                    SequentialAnimation {
                                        PauseAnimation { duration: 45 } // a beat after the first
                                        SpringAnimation { spring: Glass.springSnap; damping: 0.36; epsilon: 0.05 }
                                    }
                                }
                            }
                            onClicked: backend.addToQueue(row.index, false)
                        }
                    }
                }
            }

            // ---- Make a Mix: describe a vibe, Grok picks the songs ----
            Item {
                id: mixPane
                x: 16
                y: 64
                width: parent.width - 32
                height: parent.height - 64 - (miniBar.visible ? miniBar.height + 24 : 12)
                visible: backend.kind === "mix"
                onVisibleChanged: if (visible) {
                    if (!backend.mixBusy) mixInput.text = ""
                    mixInput.forceActiveFocus()
                }
                // Once songs start to arrive, the suggestions fold away to make room.
                readonly property bool showingSongs: mixSongs.count > 0

                Column {
                    id: mixTop
                    width: parent.width
                    spacing: 12
                    Text {
                        width: parent.width
                        text: qsTr("What are you in the mood for?")
                        color: root.ink
                        font.family: Glass.font
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        width: parent.width
                        height: 48
                        radius: 16
                        color: Glass.fill
                        border.color: mixInput.activeFocus ? "#80ffffff" : Glass.specular
                        border.width: 1
                        Text {
                            x: 16
                            anchors.verticalCenter: parent.verticalCenter
                            visible: mixInput.text.length === 0
                            text: qsTr("Late night drive, rainy Sunday…")
                            color: root.faint
                            font.family: Glass.font
                            font.pixelSize: 14
                        }
                        TextInput {
                            id: mixInput
                            x: 16
                            width: parent.width - 32
                            height: parent.height
                            verticalAlignment: TextInput.AlignVCenter
                            color: root.ink
                            font.family: Glass.font
                            font.pixelSize: 14
                            clip: true
                            maximumLength: 200
                            selectByMouse: true
                            enabled: !backend.mixBusy
                            onAccepted: backend.makeMix(text)
                        }
                    }
                    Item {
                        width: parent.width
                        height: mixPane.showingSongs ? 0 : chips.height
                        clip: true
                        opacity: mixPane.showingSongs ? 0 : 1
                        Behavior on height { SpringAnimation { spring: Glass.springPanel; damping: 0.42; epsilon: 0.1 } }
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                        Flow {
                            id: chips
                            width: parent.width
                            spacing: 8
                            // First the moods that fit this hour and day, then a few evergreens.
                            Repeater {
                                model: backend.mixChips
                                Chip {
                                    required property string modelData
                                    label: modelData
                                    tinted: true
                                    dimmed: backend.mixBusy
                                    onClicked: { mixInput.text = modelData; backend.makeMix(modelData) }
                                }
                            }
                            Repeater {
                                model: [qsTr("Late night drive"), qsTr("Deep focus"), qsTr("Road trip"), qsTr("Dinner party")]
                                Chip {
                                    required property string modelData
                                    label: modelData
                                    dimmed: backend.mixBusy
                                    onClicked: { mixInput.text = modelData; backend.makeMix(modelData) }
                                }
                            }
                        }
                    }
                    Chip {
                        width: parent.width
                        height: 50
                        strong: true
                        label: backend.mixBusy ? qsTr("Working…") : qsTr("Make mix")
                        dimmed: backend.mixBusy || mixInput.text.trim().length === 0
                        onClicked: backend.makeMix(mixInput.text)
                    }
                    Row {
                        width: parent.width
                        spacing: 10
                        visible: backend.mixStatus !== ""
                        Rectangle {
                            id: pulse
                            y: 5
                            width: 8
                            height: 8
                            radius: 4
                            color: root.ink
                            visible: backend.mixBusy
                            SequentialAnimation on opacity {
                                running: backend.mixBusy && mixPane.visible
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.25; duration: 600; easing.type: Easing.InOutQuad }
                                NumberAnimation { to: 1; duration: 600; easing.type: Easing.InOutQuad }
                            }
                        }
                        Text {
                            width: parent.width - (pulse.visible ? 18 : 0)
                            text: backend.mixStatus
                            color: root.mute
                            wrapMode: Text.WordWrap
                            font.family: Glass.font
                            font.pixelSize: 13
                        }
                    }
                    // Grok's answer as it is being written, one line of it.
                    Text {
                        width: parent.width
                        visible: backend.mixBusy && backend.mixPreview !== ""
                        text: backend.mixPreview
                        color: root.faint
                        elide: Text.ElideLeft
                        font.family: "monospace"
                        font.pixelSize: 11
                    }
                }

                ListModel { id: mixSongs }
                // Songs are added as they arrive, so only the new ones animate in.
                property int batchStart: 0
                function syncSongs() {
                    const songs = backend.mixSongs
                    if (songs.length < mixSongs.count)
                        mixSongs.clear()
                    batchStart = mixSongs.count
                    for (let i = mixSongs.count; i < songs.length; ++i)
                        mixSongs.append({ title: songs[i].title, artist: songs[i].artist })
                }
                Component.onCompleted: syncSongs()
                Connections { target: backend; function onMixChanged() { mixPane.syncSongs() } }

                ListView {
                    id: cards
                    anchors.top: mixTop.bottom
                    anchors.topMargin: 12
                    anchors.bottom: mixFoot.top
                    anchors.bottomMargin: 10
                    width: parent.width
                    clip: true
                    spacing: 6
                    model: mixSongs
                    boundsBehavior: Flickable.StopAtBounds
                    onCountChanged: if (backend.mixBusy) positionViewAtEnd()
                    delegate: Item {
                        id: card
                        required property int index
                        required property string title
                        required property string artist
                        width: cards.width
                        height: 46
                        // Each card arrives a beat after the one before it.
                        opacity: 0
                        property real rise: 16
                        transform: Translate { y: card.rise }
                        SequentialAnimation {
                            running: true
                            PauseAnimation { duration: Math.max(0, card.index - mixPane.batchStart) * 55 }
                            ParallelAnimation {
                                NumberAnimation { target: card; property: "opacity"; to: 1; duration: 220 }
                                NumberAnimation { target: card; property: "rise"; to: 0; duration: 420; easing.type: Easing.OutBack; easing.overshoot: 1.8 }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 14
                            color: Glass.fill
                            border.color: Glass.specular
                            border.width: 1
                        }
                        Text {
                            x: 12
                            width: 22
                            anchors.verticalCenter: parent.verticalCenter
                            text: card.index + 1
                            color: root.faint
                            font.family: Glass.font
                            font.pixelSize: 12
                            font.features: { "tnum": 1 }
                        }
                        Column {
                            x: 38
                            width: parent.width - 50
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                width: parent.width
                                text: card.title
                                color: root.ink
                                elide: Text.ElideRight
                                font.family: Glass.font
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                text: card.artist
                                color: root.mute
                                elide: Text.ElideRight
                                font.family: Glass.font
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Column {
                    id: mixFoot
                    anchors.bottom: parent.bottom
                    width: parent.width
                    spacing: 10
                    Chip {
                        visible: backend.mixCanSave
                        label: qsTr("Save to my library")
                        height: 38
                        pad: 20
                        onClicked: backend.saveMix()
                    }
                    Text {
                        width: parent.width
                        text: backend.useTaste
                            ? qsTr("Grok picks the songs. xAI is sent the sentence you type and a short summary of your taste (Options ▸ Taste).")
                            : qsTr("Grok picks the songs. Only the sentence you type is sent to xAI.")
                        color: root.faint
                        wrapMode: Text.WordWrap
                        font.family: Glass.font
                        font.pixelSize: 11
                    }
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: list.y + 40
                visible: list.count === 0 && backend.kind !== "mix" && !discover.shown
                text: backend.status !== "" ? backend.status : qsTr("Nothing here")
                color: root.faint
                font.family: Glass.font
                font.pixelSize: 14
            }

            // compact now-playing bar so music stays one tap away
            // Floating glass over the backdrop, not a strip: it blurs the colour mesh behind it.
            GlassPanel {
                id: miniBar
                visible: root.hasTrack && !root.searching
                x: 12
                width: parent.width - 24
                height: 60
                y: parent.height - height - 12
                radius: 18
                source: mesh
                backX: x + libraryPane.x
                backY: y
                lightSpill: false
                Cover {
                    id: thumb
                    x: 8
                    y: 8
                    width: 44
                    height: 44
                    radius: 11
                }
                Column {
                    anchors.left: thumb.right
                    anchors.leftMargin: 12
                    anchors.right: barPlay.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        text: backend.track
                        color: root.ink
                        elide: Text.ElideRight
                        font.family: Glass.font
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Text {
                        width: parent.width
                        text: backend.artist
                        color: root.mute
                        elide: Text.ElideRight
                        font.family: Glass.font
                        font.pixelSize: 12
                    }
                }
                MouseArea {
                    anchors.left: parent.left
                    anchors.right: barPlay.left
                    height: parent.height
                    cursorShape: Qt.PointingHandCursor
                    onClicked: backend.openPlayer()
                }
                IconButton {
                    id: barPlay
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    playState: backend.playing ? 1 : 0
                    size: 22
                    hit: 44
                    onClicked: backend.playPause()
                }
            }
        }

        // ================= THE COVER IN FLIGHT between the big player and the mini player =================
        // While the window morphs, one cover travels from the big artwork's place to the mini one's (and
        // back), growing or shrinking on the way, so the art is clearly the same thing in both layouts.
        // When the window jumps straight from one to the other (carried to another desktop), there is no
        // in-between and nothing flies.
        Cover {
            id: flyer
            property bool fromNow: false
            readonly property real m: root.window ? Math.max(0, Math.min(1, root.window.morph)) : (root.mini ? 1 : 0)
            readonly property bool flying: fromNow && m > 0.002 && m < 0.998
            readonly property real bigSize: Math.min(root.width - 48, (root.window ? root.window.fullHeight : 640) - 358)
            function mix(a, b) { return a + (b - a) * m }
            visible: flying
            z: 5
            texSize: 600
            x: mix((root.width - bigSize) / 2, 16)
            y: mix(60, 16)
            width: mix(bigSize, 64)
            height: width
            radius: mix(22, 14)
        }

        // ================= MINI: art, song and waveform; controls on hover =================
        Item {
            id: miniPane
            anchors.fill: parent
            opacity: root.mini ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            // 1 while the pointer is over the mini player, with a springy change.
            property real reveal: root.mini && root.hovering ? 1 : 0
            Behavior on reveal { SpringAnimation { spring: Glass.springBouncy; damping: 0.3; epsilon: 0.002 } }
            readonly property real pump: Glass.reactive ? 0.08 * Glass.cover : 0

            Cover {
                id: miniArt
                opacity: flyer.flying ? 0 : 1
                x: 16
                y: 16
                width: 64
                height: 64
                radius: 14
                scale: backend.playing ? 1 : 0.92
                Behavior on scale { SpringAnimation { spring: 2.6; damping: 0.2; epsilon: 0.001 } }
                transform: Scale {
                    origin.x: 32
                    origin.y: 32
                    xScale: 1 + miniPane.pump
                    yScale: 1 + miniPane.pump
                }
            }
            // at rest: the song and the artist
            Column {
                x: 94
                y: 15
                width: parent.width - 94 - 16
                spacing: 2
                opacity: 1 - Math.min(1, miniPane.reveal * 1.6)
                visible: opacity > 0.01
                transform: Translate { y: -6 * miniPane.reveal }
                Text {
                    width: parent.width
                    text: root.hasTrack ? backend.track : qsTr("Nothing playing")
                    color: root.ink
                    elide: Text.ElideRight
                    font.family: Glass.font
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    text: backend.artist
                    color: root.mute
                    elide: Text.ElideRight
                    font.family: Glass.font
                    font.pixelSize: 12
                    visible: text !== ""
                }
            }
            // under the pointer: previous, play, next, each arriving a moment apart
            Row {
                x: 88
                y: 10
                spacing: 2
                visible: miniPane.reveal > 0.02
                opacity: Math.min(1, miniPane.reveal)
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    path: "M6 6h2v12H6zM9.5 12l8.5 6V6z"
                    size: 20
                    hit: 40
                    transform: Translate { y: 10 * (1 - miniPane.reveal) }
                    onClicked: backend.previousTrack()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    playState: backend.playing ? 1 : 0
                    size: 22
                    hit: 44
                    filled: true
                    fill: root.ink
                    tint: Glass.inkText
                    transform: Translate { y: 16 * (1 - miniPane.reveal) }
                    onClicked: backend.playPause()
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    path: "M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z"
                    size: 20
                    hit: 40
                    transform: Translate { y: 22 * (1 - miniPane.reveal) }
                    onClicked: backend.nextTrack()
                }
            }
            LiquidScrubber {
                x: 94
                y: 60
                width: parent.width - 94 - 18
                compact: true
                style: root.visualStyle
                position: backend.position
                duration: backend.duration
                playing: backend.playing
                trackKey: root.trackKey
                onSeekRequested: function (seconds) { backend.seek(seconds) }
            }
        }
    }
    }

    // ================= SETTINGS (animation, autoplay, taste, desktops) and WRAPPED =================
    MouseArea {
        anchors.fill: parent
        z: 8
        visible: root.styleOpen
        onClicked: root.styleOpen = false
    }

    // One compact setting: a name, one quiet line under it, and a switch.
    component SettingRow: Item {
        id: setting
        property string title: ""
        property string note: ""
        property bool checked: false
        property bool interactive: true
        signal toggled()
        width: parent ? parent.width : 0
        height: 58
        Rectangle { width: parent.width - 12; x: 6; height: 1; color: Glass.specular }
        Text {
            x: 12
            y: 10
            text: setting.title
            color: Glass.ink
            font.family: Glass.font
            font.pixelSize: 15
            font.weight: setting.checked ? Font.DemiBold : Font.Normal
        }
        Text {
            x: 12
            y: 32
            width: parent.width - 84
            text: setting.note
            color: Glass.mute
            elide: Text.ElideRight
            font.family: Glass.font
            font.pixelSize: 11
        }
        LiquidSwitch {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 13
            checked: setting.checked
            interactive: setting.interactive
            onToggled: setting.toggled()
        }
    }

    component SmallLabel: Text {
        color: Glass.faint
        font.family: Glass.font
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }

    GlassPanel {
        id: styleSheet
        z: 9
        source: stage
        x: 12
        y: 56
        width: parent.width - 24
        readonly property real wantHeight: 52 + (root.settingsTab === "general" ? generalTab.height : Math.min(wrappedFlick.contentHeight, root.height - 56 - 12 - 52 - 8)) + 8
        height: wantHeight
        Behavior on height { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
        radius: 22
        opacity: root.styleOpen ? 1 : 0
        scale: root.styleOpen ? 1 : 0.9
        transformOrigin: Item.TopLeft
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Behavior on scale { SpringAnimation { spring: Glass.springPanel; damping: Glass.dampingPanel; epsilon: 0.002 } }
        MouseArea { anchors.fill: parent }

        // two tabs, one sheet
        Row {
            x: 16
            y: 12
            spacing: 8
            Chip {
                label: qsTr("Settings")
                height: 30
                strong: root.settingsTab === "general"
                onClicked: root.settingsTab = "general"
            }
            Chip {
                label: qsTr("Wrapped")
                height: 30
                strong: root.settingsTab === "wrapped"
                onClicked: root.settingsTab = "wrapped"
            }
        }

        // ---- Settings tab ----
        Column {
            id: generalTab
            x: 8
            y: 52
            width: parent.width - 16
            opacity: root.settingsTab === "general" ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 160 } }

            Item {
                width: parent.width
                height: animFlow.implicitHeight + 62
                SmallLabel { x: 12; y: 2; text: qsTr("Animation") }
                Flow {
                    id: animFlow
                    x: 8
                    y: 24
                    width: parent.width - 16
                    spacing: 8
                    Repeater {
                        model: root.visualStyles
                        Chip {
                            required property var modelData
                            label: modelData.label
                            strong: root.visualStyle === modelData.id
                            onClicked: root.visualStyle = modelData.id
                        }
                    }
                }
                Text {
                    x: 12
                    y: animFlow.y + animFlow.implicitHeight + 8
                    width: parent.width - 24
                    text: {
                        for (const style of root.visualStyles)
                            if (style.id === root.visualStyle)
                                return style.hint
                        return ""
                    }
                    color: Glass.mute
                    elide: Text.ElideRight
                    font.family: Glass.font
                    font.pixelSize: 11
                }
            }
            SettingRow {
                title: qsTr("Play similar songs")
                note: !backend.similarAvailable ? qsTr("Needs the Grok CLI.")
                    : backend.similarStatus !== "" ? backend.similarStatus
                    : qsTr("Keeps up to 10 songs queued, like the one you picked")
                checked: backend.autoSimilar
                interactive: backend.similarAvailable
                onToggled: backend.autoSimilar = !backend.autoSimilar
            }
            SettingRow {
                title: qsTr("Learn what I like")
                note: backend.useTaste ? backend.tasteSummary : qsTr("Off. Nothing is remembered.")
                checked: backend.useTaste
                onToggled: backend.useTaste = !backend.useTaste
            }
            SettingRow {
                visible: root.showFollow
                height: visible ? 58 : 0
                title: qsTr("Follow me across desktops")
                note: qsTr("Carries the player along as the mini player")
                checked: desktop.follow
                onToggled: desktop.follow = !desktop.follow
            }
        }

        // ---- Wrapped tab ----
        Flickable {
            id: wrappedFlick
            x: 8
            y: 52
            width: parent.width - 16
            height: parent.height - 52 - 8
            contentWidth: width
            contentHeight: wrapped.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            opacity: root.settingsTab === "wrapped" ? 1 : 0
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 160 } }

            Column {
                id: wrapped
                width: parent.width
                spacing: 10
                readonly property var stats: backend.stats
                readonly property var songs: stats.topSongs
                readonly property var artists: stats.topArtists
                readonly property var week: stats.week
                readonly property real weekMax: {
                    let top = 1
                    for (const day of week)
                        top = Math.max(top, day.seconds)
                    return top
                }

                // empty until there is something to say
                Text {
                    visible: !wrapped.stats.hasData
                    width: parent.width
                    topPadding: 24
                    bottomPadding: 28
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Listen for a while and your Wrapped fills in here: time, favourites and streaks. It stays on this machine.")
                    color: Glass.mute
                    font.family: Glass.font
                    font.pixelSize: 13
                    leftPadding: 20
                    rightPadding: 20
                }

                // the headline
                Rectangle {
                    visible: wrapped.stats.hasData
                    width: parent.width
                    height: visible ? 92 : 0
                    radius: 18
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.55) }
                        GradientStop { position: 1; color: Qt.rgba(Glass.accentLight.r, Glass.accentLight.g, Glass.accentLight.b, 0.18) }
                    }
                    border.width: 1
                    border.color: Glass.specular
                    SmallLabel { x: 16; y: 12; text: qsTr("You have listened for"); color: Glass.mute }
                    Text {
                        x: 16
                        y: 30
                        text: wrapped.stats.total
                        color: Glass.ink
                        font.family: Glass.font
                        font.pixelSize: 34
                        font.weight: Font.Bold
                    }
                    Text {
                        x: 16
                        y: 70
                        text: qsTr("%1 today").arg(wrapped.stats.today)
                        color: Glass.mute
                        font.family: Glass.font
                        font.pixelSize: 11
                    }
                }

                // three small numbers
                Row {
                    visible: wrapped.stats.hasData
                    width: parent.width
                    height: visible ? 58 : 0
                    spacing: 8
                    Repeater {
                        model: [
                            { value: String(wrapped.stats.plays), name: qsTr("plays") },
                            { value: wrapped.stats.streak > 0 ? String(wrapped.stats.streak) : "–", name: qsTr("day streak") },
                            { value: wrapped.stats.peak !== "" ? wrapped.stats.peak : "–", name: qsTr("you listen most") }
                        ]
                        Rectangle {
                            required property var modelData
                            width: (wrapped.width - 16) / 3
                            height: 58
                            radius: 14
                            color: Glass.fill
                            border.width: 1
                            border.color: Glass.specular
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 9
                                width: parent.width - 8
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: parent.modelData.value
                                color: Glass.ink
                                font.family: Glass.font
                                font.pixelSize: 17
                                font.weight: Font.DemiBold
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                y: 34
                                text: parent.modelData.name
                                color: Glass.faint
                                font.family: Glass.font
                                font.pixelSize: 10
                            }
                        }
                    }
                }

                // the last seven days
                Item {
                    visible: wrapped.stats.hasData
                    width: parent.width
                    height: visible ? 64 : 0
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 8
                        Repeater {
                            model: wrapped.week
                            Item {
                                required property var modelData
                                required property int index
                                width: (wrapped.width - 12 - 6 * 8) / 7
                                height: 64
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: parent.width
                                    height: Math.max(4, 42 * parent.modelData.seconds / wrapped.weekMax)
                                    y: 44 - height
                                    radius: 5
                                    color: parent.index === 6 ? Glass.accentLight : Glass.rail
                                    Behavior on height { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 49
                                    text: parent.modelData.day
                                    color: Glass.faint
                                    font.family: Glass.font
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

                // favourites
                SmallLabel { visible: wrapped.stats.hasData && wrapped.songs.length > 0; leftPadding: 6; text: qsTr("Most played") }
                Repeater {
                    model: wrapped.stats.hasData ? wrapped.songs.slice(0, 3) : []
                    Item {
                        id: songRow
                        required property var modelData
                        required property int index
                        width: wrapped.width
                        height: 44
                        Rectangle {
                            id: cover
                            x: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 38
                            height: 38
                            radius: 8
                            color: songRow.index === 0 ? Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.5) : Glass.fill
                            clip: true
                            Image {
                                id: coverImage
                                anchors.fill: parent
                                source: songRow.modelData.artwork || ""
                                visible: status === Image.Ready
                                fillMode: Image.PreserveAspectCrop
                            }
                            Text {
                                anchors.centerIn: parent
                                text: String(songRow.index + 1)
                                visible: !coverImage.visible
                                color: Glass.ink
                                font.family: Glass.font
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }
                        }
                        Column {
                            x: 54
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 54 - 70
                            Text {
                                width: parent.width
                                text: songRow.modelData.title
                                elide: Text.ElideRight
                                color: Glass.ink
                                font.family: Glass.font
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                width: parent.width
                                text: songRow.modelData.artist
                                elide: Text.ElideRight
                                color: Glass.mute
                                font.family: Glass.font
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: songRow.modelData.time
                            color: Glass.faint
                            font.family: Glass.font
                            font.pixelSize: 11
                        }
                    }
                }
                SmallLabel { visible: wrapped.stats.hasData && wrapped.artists.length > 0; leftPadding: 6; text: qsTr("Top artists") }
                Flow {
                    visible: wrapped.stats.hasData
                    width: parent.width
                    spacing: 6
                    Repeater {
                        model: wrapped.stats.hasData ? wrapped.artists.slice(0, 3) : []
                        Chip {
                            required property var modelData
                            height: 28
                            pad: 12
                            label: modelData.name
                        }
                    }
                }
                Chip {
                    visible: wrapped.stats.hasData
                    height: 26
                    pad: 12
                    dimmed: true
                    label: qsTr("Reset my stats")
                    onClicked: backend.forgetStats()
                }
                Item { width: 1; height: 4 }
            }
        }
    }

    // ================= LYRICS =================
    // The one writer of wantLyrics: the sheet, or the full-screen view.
    Binding { target: backend; property: "wantLyrics"; value: root.lyricsOpen || (root.window !== null && root.window.cinema) }
    Rectangle {
        anchors.fill: parent
        z: 8
        color: Glass.scrim
        opacity: root.lyricsOpen ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 240 } }
        MouseArea { anchors.fill: parent; onClicked: root.lyricsOpen = false }
    }
    GlassPanel {
        id: lyricsSheet
        z: 9
        source: stage
        width: parent.width - 16
        x: 8
        height: parent.height - 120
        y: root.lyricsOpen ? 56 : parent.height + 24
        visible: y < parent.height + 20
        Behavior on y { SpringAnimation { spring: Glass.springPanel; damping: Glass.dampingPanel; epsilon: 0.2 } }
        MouseArea { anchors.fill: parent }
        LyricsView {
            anchors.fill: parent
            anchors.margins: 18
            fontSize: 21
        }
    }

    // ================= SOUND =================
    Rectangle {
        id: scrim
        anchors.fill: parent
        z: 8
        color: Glass.scrim
        opacity: root.soundOpen ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 240 } }
        MouseArea { anchors.fill: parent; onClicked: root.soundOpen = false }
    }
    AdvancedSoundStudio {
        id: sheet
        z: 9
        source: stage
        width: parent.width - 16
        x: 8
        height: implicitHeight
        y: root.soundOpen ? parent.height - height - 8 : parent.height + 24
        visible: y < parent.height + 20
        Behavior on y { SpringAnimation { spring: Glass.springPanel; damping: Glass.dampingPanel; epsilon: 0.2 } }
        onCloseRequested: root.soundOpen = false
    }

    // ================= WELCOME (first run, signed out) =================
    Item {
        id: welcome
        anchors.fill: parent
        z: 7
        visible: root.signedOut && !root.mini
        opacity: visible ? 1 : 0
        Rectangle { anchors.fill: parent; color: "#33000000" }
        MouseArea { anchors.fill: parent; onPressed: if (root.window) root.window.startSystemMove() }
        // The name, drawn as strokes (see DriftMark). While the opening plays, the opening's own copy is shown instead,
        // and glides into this spot.
        Item {
            id: logo
            width: 220
            height: crest.height
            x: (parent.width - width) / 2
            y: 70
            DriftMark {
                id: crest
                markWidth: 220
                running: welcome.visible && !root.introActive
                opacity: root.introActive ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 120 } }
            }
        }
        Text {
            y: logo.y + logo.height + 22
            x: 36
            width: parent.width - 72
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Your Apple Music, in a beautiful player that moves with the music.")
            color: root.mute
            font.family: Glass.font
            font.pixelSize: 15
        }
        Column {
            x: 56
            y: logo.y + logo.height + 96
            width: parent.width - 112
            spacing: 12
            Repeater {
                model: [qsTr("Search, queue and play anything"), qsTr("Grok-made mixes from one sentence"), qsTr("A 10-band EQ studio, built in"), qsTr("Ctrl+K runs any command")]
                delegate: Row {
                    required property string modelData
                    spacing: 12
                    Rectangle { y: 7; width: 6; height: 6; radius: 3; color: Glass.accentLight }
                    Text {
                        text: parent.modelData
                        color: root.ink
                        font.family: Glass.font
                        font.pixelSize: 14
                        opacity: 0.9
                    }
                }
            }
        }
        Chip {
            id: signIn
            x: 36
            y: parent.height - 120
            width: parent.width - 72
            height: 54
            strong: true
            label: qsTr("Sign in with Apple Music")
            onClicked: backend.center()
        }
        Text {
            y: signIn.y + signIn.height + 14
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: backend.status !== "" ? backend.status : qsTr("Needs an Apple Music subscription. Your sign-in stays on this machine.")
            color: root.faint
            wrapMode: Text.WordWrap
            font.family: Glass.font
            font.pixelSize: 11
            leftPadding: 30
            rightPadding: 30
        }
    }

    // ================= COMMAND PALETTE (Ctrl+K) =================
    CommandPalette {
        id: commandPalette
        z: 11
        source: stage
        open: root.paletteOpen
        items: root.paletteOpen ? root.buildCommands() : []
        onTriggered: function (id, arg) { root.runCommand(id, arg) }
        onDismissed: root.paletteOpen = false
    }

    // window controls for the frameless window, above both panes.
    // The mini player keeps them out of sight until the pointer arrives.
    Row {
        id: chrome
        z: 12
        anchors.right: parent.right
        anchors.rightMargin: 10
        y: root.mini ? 8 : 16
        spacing: 2
        opacity: !root.mini || root.hovering ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 160 } }
        HeaderPlusPill {
            visible: !root.mini && backend.signedIn
            open: root.plusOpen
            anchors.verticalCenter: undefined
            onClicked: { root.plusOpen = !root.plusOpen; if (root.plusOpen) { root.soundOpen = false; root.styleOpen = false } }
        }
        // Settings and Wrapped: one place for animation, autoplay, taste and your listening.
        IconButton {
            visible: !root.mini && backend.signedIn
            path: "M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.488.488 0 0 0-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54a.484.484 0 0 0-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"
            size: 16
            hit: 32
            tint: root.styleOpen ? Glass.accentLight : root.mute
            onClicked: { root.styleOpen = !root.styleOpen; if (root.styleOpen) { root.plusOpen = false; root.soundOpen = false } }
        }
        IconButton {
            path: root.mini ? "M7 14H5v5h5v-2H7v-3zm-2-4h2V7h3V5H5v5zm12 7h-3v2h5v-5h-2v3zM14 5v2h3v3h2V5h-5z"
                            : "M5 16h3v3h2v-5H5v2zm3-8H5v2h5V5H8v3zm6 11h2v-3h3v-2h-5v5zm2-11V5h-2v5h5V8h-3z"
            size: 16
            hit: 32
            tint: root.mute
            onClicked: if (root.window) root.window.mini = !root.window.mini
        }
        IconButton {
            path: "M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"
            size: 16
            hit: 32
            tint: root.mute
            onClicked: {
                if (root.window && root.window.rememberPosition) root.window.rememberPosition()
                Qt.quit()
            }
        }
    }

    // Closes the + menu on any tap outside it.
    MouseArea {
        anchors.fill: parent
        z: 12.5
        enabled: root.plusOpen
        onClicked: root.plusOpen = false
    }
    ActionDropdown {
        id: plusMenu
        z: 13
        source: stage
        open: root.plusOpen && !root.mini
        x: parent.width - width - 10
        y: chrome.y + 40
        backX: x
        backY: y
        onDismissed: root.plusOpen = false
    }

    // brief confirmation, e.g. "Added to Up Next"
    Rectangle {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - (root.mini ? 46 : 120)
        z: 10
        height: 38
        width: toastText.implicitWidth + 36
        radius: height / 2
        color: "#e6f5f5f7"
        opacity: backend.toast !== "" ? 1 : 0
        visible: opacity > 0.01
        scale: backend.toast !== "" ? 1 : 0.8
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
        property string last: ""
        Connections { target: backend; function onToastChanged() { if (backend.toast !== "") toast.last = backend.toast } }
        Text {
            id: toastText
            anchors.centerIn: parent
            text: toast.last
            color: Glass.inkText
            font.family: Glass.font
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }
}
