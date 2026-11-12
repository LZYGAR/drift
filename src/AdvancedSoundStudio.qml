import QtQuick
import QtQuick.Shapes
import QtQuick.Effects

// The Sound studio. Simple mode is a three-band EQ (bass, mid, treble) you shape by dragging points on its
// curve; Advanced mode swaps them for ten parametric bands (31 Hz to 16 kHz, +-12 dB). Both add a reverb
// amount and a drawer of presets. The curve is worked out from the same filter maths the PipeWire graph
// uses, so it shows what the sliders will do, and the live spectrum of what is playing ripples across it.
GlassPanel {
    id: stage

    signal closeRequested()
    readonly property bool adv: effects.advanced
    property bool syncOpen: false
    implicitHeight: syncOpen ? tapPad.y + 64 + 22 : syncToggle.y + syncToggle.height + 18
    Behavior on implicitHeight { SpringAnimation { spring: Glass.springPanel; damping: 0.4; epsilon: 0.5 } }

    function touch() { if (!effects.enabled) effects.enabled = true }

    MouseArea { anchors.fill: parent }   // keep taps inside the sheet

    component FxSlider: Item {
        id: fx
        property string label: ""
        property real value: 0
        property real from: 0
        property real to: 1
        property string unit: ""
        signal moved(real v)
        width: parent ? parent.width : 0
        height: 46
        readonly property real ratio: (value - from) / (to - from)
        Text {
            text: fx.label
            color: Glass.ink
            font.family: Glass.font
            font.pixelSize: 14
            font.weight: Font.Medium
        }
        Text {
            anchors.right: parent.right
            text: fx.unit
            color: Glass.mute
            font.family: Glass.font
            font.pixelSize: 12
            font.features: { "tnum": 1 }
        }
        Rectangle {
            y: 30
            width: parent.width
            height: 4
            radius: 2
            color: Glass.rail
            Rectangle {
                width: parent.width * fx.ratio
                height: parent.height
                radius: 2
                color: Glass.accentLight
            }
            Rectangle {
                x: parent.width * fx.ratio - width / 2
                y: (parent.height - height) / 2
                width: 16
                height: 16
                radius: 8
                color: Glass.ink
                scale: drag.pressed ? 1.3 : 1
                Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
            }
        }
        MouseArea {
            id: drag
            y: 18
            width: parent.width
            height: 28
            cursorShape: Qt.PointingHandCursor
            function apply(mx) { fx.moved(fx.from + Math.max(0, Math.min(1, mx / width)) * (fx.to - fx.from)) }
            onPressed: function (mouse) { apply(mouse.x) }
            onPositionChanged: function (mouse) { if (pressed) apply(mouse.x) }
        }
    }

    // ---- header ----
    Item {
        id: head
        x: 22
        y: 18
        width: parent.width - 44
        height: 34
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Sound")
            color: Glass.ink
            font.family: Glass.font
            font.pixelSize: 20
            font.weight: Font.Bold
        }
        LiquidSwitch {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            checked: effects.enabled
            interactive: effects.available
            onToggled: effects.enabled = !effects.enabled
        }
    }
    Text {
        id: note
        x: 22
        y: head.y + head.height + 2
        width: parent.width - 44 - modeSwitch.width - 8
        height: modeSwitch.height
        verticalAlignment: Text.AlignVCenter
        text: !effects.available ? qsTr("Needs PipeWire (pipewire, pw-cli, pactl).")
            : effects.enabled && !effects.active ? qsTr("Starting…")
            : effects.enabled ? qsTr("Shaping this app's audio only.")
            : qsTr("Off. Drag to turn on.")
        color: Glass.faint
        font.family: Glass.font
        font.pixelSize: 11
        // Two lines, so the whole sentence can be read next to the Simple | Advanced switch.
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
    }

    // Simple | Advanced
    Rectangle {
        id: modeSwitch
        x: parent.width - width - 22
        y: note.y
        width: 138
        height: 26
        radius: 13
        color: Glass.fill
        border.color: Glass.specular
        border.width: 1
        Rectangle {
            x: stage.adv ? parent.width / 2 : 0
            width: parent.width / 2
            height: parent.height
            radius: 13
            color: Glass.ink
            Behavior on x { SpringAnimation { spring: Glass.springPill; damping: Glass.dampingPill; epsilon: 0.2 } }
        }
        Repeater {
            model: [{ label: qsTr("Simple"), advanced: false }, { label: qsTr("Advanced"), advanced: true }]
            Item {
                required property var modelData
                x: modelData.advanced ? modeSwitch.width / 2 : 0
                width: modeSwitch.width / 2
                height: modeSwitch.height
                Text {
                    anchors.centerIn: parent
                    text: parent.modelData.label
                    color: stage.adv === parent.modelData.advanced ? Glass.inkText : Glass.ink
                    font.family: Glass.font
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: effects.advanced = parent.modelData.advanced
                }
            }
        }
    }

    // ---- the curve ----
    Rectangle {
        id: plot
        x: 16
        y: note.y + 34
        width: parent.width - 32
        height: 168
        radius: 18
        color: "#40000000"
        border.color: Glass.specular
        border.width: 1
        opacity: effects.enabled ? 1 : 0.6
        Behavior on opacity { NumberAnimation { duration: 200 } }

        readonly property real pad: 14
        readonly property real graphTop: 16
        readonly property real graphHeight: 104
        readonly property real mid: graphTop + graphHeight / 2
        readonly property real range: 12   // dB above and below the centre line
        readonly property int points: 56
        // Rebuilt whenever a band moves.
        readonly property var gains: {
            effects.bass; effects.mid; effects.treble; effects.bands; effects.advanced
            return effects.curve(points)
        }
        function xAt(ratio) { return pad + ratio * (width - pad * 2) }
        function yAt(db) { return mid - Math.max(-range, Math.min(range, db)) / range * (graphHeight / 2) }
        // Where a frequency sits on the log scale from 20 Hz to 20 kHz.
        function ratioOf(hz) { return Math.log(hz / 20) / Math.log(1000) }
        readonly property var line: {
            const out = []
            for (let i = 0; i < gains.length; ++i)
                out.push(Qt.point(xAt(i / (gains.length - 1)), yAt(gains[i])))
            return out
        }
        readonly property var area: {
            const out = line.slice()
            out.push(Qt.point(xAt(1), mid))
            out.push(Qt.point(xAt(0), mid))
            if (out.length > 2)
                out.push(out[0])
            return out
        }

        // grid: the 0 dB line and one line each at +6 and -6
        Repeater {
            model: [6, 0, -6]
            Rectangle {
                required property int modelData
                x: plot.pad
                y: plot.yAt(modelData)
                width: plot.width - plot.pad * 2
                height: 1
                color: modelData === 0 ? "#40ffffff" : "#1affffff"
            }
        }
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 0; y1: plot.graphTop
                    x2: 0; y2: plot.graphTop + plot.graphHeight
                    GradientStop { position: 0; color: Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.5) }
                    GradientStop { position: 0.5; color: Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.12) }
                    GradientStop { position: 1; color: Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.5) }
                }
                PathPolyline { path: plot.area }
            }
            ShapePath {
                strokeColor: Glass.accentLight
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: plot.line }
            }
        }

        // The live spectrum: 24 bands, 40 Hz to 16 kHz, as a ribbon behind the curve. Rises at once on a
        // transient, falls with exponential gravity (about 300 ms to halve) so it never flickers.
        property var ribbonLevels: new Array(24).fill(0)
        readonly property real ribbonGravity: 0.0023   // per millisecond: level *= exp(-gravity * dt) ~ 0.5 in 300 ms
        FrameAnimation {
            running: stage.visible && Glass.reactive
            onTriggered: {
                const dt = frameTime * 1000
                const keep = Math.exp(-plot.ribbonGravity * dt)
                const now = audio.spectrum
                const next = plot.ribbonLevels.slice()
                for (let i = 0; i < next.length; ++i) {
                    const target = i < now.length ? now[i] : 0
                    next[i] = Math.max(target, next[i] * keep)
                }
                plot.ribbonLevels = next
            }
        }
        // Catmull-Rom through the 24 points, so the ribbon is a smooth spline rather than a zig-zag.
        readonly property var ribbon: {
            const lv = ribbonLevels
            const n = lv.length
            const base = graphTop + graphHeight
            const pts = []
            for (let i = 0; i < n; ++i) {
                const hz = 40 * Math.pow(400, i / (n - 1))
                pts.push(Qt.point(xAt(ratioOf(hz)), base - Math.min(1, lv[i]) * graphHeight * 0.85))
            }
            const out = []
            for (let i = 0; i < n - 1; ++i) {
                const p0 = pts[Math.max(0, i - 1)], p1 = pts[i], p2 = pts[i + 1], p3 = pts[Math.min(n - 1, i + 2)]
                for (let t = 0; t < 1; t += 0.25) {
                    const t2 = t * t, t3 = t2 * t
                    out.push(Qt.point(
                        0.5 * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
                        Math.min(base, 0.5 * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3))))
                }
            }
            out.push(pts[n - 1])
            return out
        }
        readonly property var ribbonArea: {
            const base = graphTop + graphHeight
            const out = ribbon.slice()
            if (out.length > 1) {
                out.push(Qt.point(out[out.length - 1].x, base))
                out.push(Qt.point(out[0].x, base))
                out.push(out[0])
            }
            return out
        }
        Shape {
            anchors.fill: parent
            visible: Glass.reactive && audio.active
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 0; y1: plot.graphTop
                    x2: 0; y2: plot.graphTop + plot.graphHeight
                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.20) }
                    GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0.0) }
                }
                PathPolyline { path: plot.ribbonArea }
            }
            ShapePath {
                strokeColor: Qt.rgba(1, 1, 1, 0.55)
                strokeWidth: 1.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: plot.ribbon }
            }
        }

        // Ten parametric bands (Advanced): a point per band, with its frequency and gain readout beneath.
        Repeater {
            model: stage.adv ? effects.bandFrequencies : []
            Item {
                id: pband
                required property int index
                required property var modelData
                readonly property real db: effects.bands[index]
                x: plot.xAt(plot.ratioOf(modelData))
                width: 1
                height: plot.height
                Rectangle {
                    x: -width / 2
                    y: plot.yAt(pband.db) - height / 2
                    width: 16
                    height: 16
                    radius: 8
                    color: Glass.ink
                    border.color: Glass.accent
                    border.width: 3
                    scale: pgrab.pressed ? 1.35 : (pgrab.containsMouse ? 1.15 : 1)
                    Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
                }
                Column {
                    x: -width / 2
                    y: plot.graphTop + plot.graphHeight + 6
                    spacing: 0
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: pband.modelData >= 1000 ? (pband.modelData / 1000) + "k" : pband.modelData
                        color: Glass.ink
                        font.family: Glass.font
                        font.pixelSize: 10
                        font.weight: Font.Medium
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (pband.db > 0.05 ? "+" : "") + pband.db.toFixed(1)
                        color: pgrab.pressed ? Glass.ink : Glass.mute
                        font.family: Glass.font
                        font.pixelSize: 10
                        font.features: { "tnum": 1 }
                    }
                }
                MouseArea {
                    id: pgrab
                    x: -13
                    y: plot.graphTop - 10
                    width: 26
                    height: plot.graphHeight + 20
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    function apply(my) {
                        let db = (plot.mid - (y + my)) / (plot.graphHeight / 2) * plot.range
                        if (Math.abs(db) < 0.4) db = 0
                        effects.setBand(pband.index, db)
                        stage.touch()
                    }
                    onPressed: function (mouse) { apply(mouse.y) }
                    onPositionChanged: function (mouse) { if (pressed) apply(mouse.y) }
                    onDoubleClicked: effects.setBand(pband.index, 0)
                }
            }
        }

        // The three bands (Simple), as points you drag up and down.
        Repeater {
            model: stage.adv ? [] : [
                { name: qsTr("Bass"), hz: 100, max: 10, key: "bass" },
                { name: qsTr("Mid"), hz: 1000, max: 8, key: "mid" },
                { name: qsTr("Treble"), hz: 8000, max: 10, key: "treble" }
            ]
            Item {
                id: band
                required property var modelData
                readonly property real value: effects[modelData.key]
                readonly property real db: value * modelData.max
                x: plot.xAt(plot.ratioOf(modelData.hz))
                width: 1
                height: plot.height
                // How loud this band of the music is right now: the ribbon glows there with it.
                readonly property real live: !Glass.reactive ? 0
                    : modelData.key === "bass" ? Glass.bass : modelData.key === "mid" ? Glass.mid : Glass.treble
                Rectangle {
                    x: -width / 2
                    y: plot.yAt(band.db) - height / 2
                    width: 64
                    height: 64
                    radius: 32
                    color: Glass.accentLight
                    opacity: 0.08 + 0.5 * band.live
                    scale: 0.7 + 0.6 * band.live
                    layer.enabled: true
                    layer.effect: MultiEffect { blurEnabled: true; blur: 1.0; blurMax: 32; autoPaddingEnabled: true }
                }
                Rectangle {
                    id: knob
                    x: -width / 2
                    y: plot.yAt(band.db) - height / 2
                    width: 20
                    height: 20
                    radius: 10
                    color: Glass.ink
                    border.color: Glass.accent
                    border.width: 3
                    scale: grab.pressed ? 1.3 : (grab.containsMouse ? 1.12 : 1)
                    Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
                }
                Column {
                    x: -width / 2
                    y: plot.graphTop + plot.graphHeight + 8
                    spacing: 0
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: band.modelData.name
                        color: Glass.ink
                        font.family: Glass.font
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (band.db >= 0 ? "+" : "") + band.db.toFixed(1) + " dB"
                        color: Glass.mute
                        font.family: Glass.font
                        font.pixelSize: 11
                        font.features: { "tnum": 1 }
                    }
                }
                MouseArea {
                    id: grab
                    x: -26
                    y: plot.graphTop - 10
                    width: 52
                    height: plot.graphHeight + 20
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    function apply(my) {
                        let db = (plot.mid - (y + my)) / (plot.graphHeight / 2) * plot.range
                        // Sticks to the centre line, so flat is easy to hit.
                        if (Math.abs(db) < 0.5) db = 0
                        effects[band.modelData.key] = Math.max(-1, Math.min(1, db / band.modelData.max))
                        stage.touch()
                    }
                    onPressed: function (mouse) { apply(mouse.y) }
                    onPositionChanged: function (mouse) { if (pressed) apply(mouse.y) }
                    onDoubleClicked: { effects[band.modelData.key] = 0 }
                }
            }
        }
    }

    // ---- reverb ----
    FxSlider {
        id: room
        x: 22
        y: plot.y + plot.height + 16
        width: parent.width - 44
        label: qsTr("Reverb")
        value: effects.reverb
        unit: Math.round(effects.reverb * 100) + "%"
        onMoved: function (v) { effects.reverb = v; stage.touch() }
    }

    // ---- presets drawer: one tap, then it is applied; swipe or wheel for the rest ----
    Flickable {
        id: presetFlow
        x: 0
        y: room.y + room.height + 14
        width: parent.width
        height: 34
        contentWidth: presetRow.width + 32
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        WheelHandler {
            orientation: Qt.Vertical
            onWheel: function (event) {
                presetFlow.contentX = Math.max(0, Math.min(presetFlow.contentWidth - presetFlow.width, presetFlow.contentX - event.angleDelta.y))
            }
        }
        Row {
            id: presetRow
            x: 16
            spacing: 8
            Repeater {
                model: effects.presets
                Rectangle {
                    id: chip
                    required property string modelData
                    readonly property bool current: effects.preset === modelData
                    height: 34
                    width: chipText.implicitWidth + 26
                    radius: 17
                    color: current ? Glass.ink : (chipArea.containsMouse ? Glass.fillHover : Glass.fill)
                    border.color: current ? "transparent" : Glass.specular
                    border.width: 1
                    scale: chipArea.pressed ? 0.94 : 1
                    Behavior on scale { SpringAnimation { spring: Glass.springBouncy; damping: Glass.dampingBouncy; epsilon: 0.002 } }
                    Behavior on color { ColorAnimation { duration: 160 } }
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: chip.modelData
                        color: chip.current ? Glass.inkText : Glass.ink
                        font.family: Glass.font
                        font.pixelSize: 13
                        font.weight: chip.current ? Font.DemiBold : Font.Normal
                    }
                    MouseArea {
                        id: chipArea
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: effects.available
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            effects.applyPreset(chip.modelData)
                            // "Flat Studio" changes nothing audible, so it does not need the graph running.
                            if (chip.modelData !== "Flat Studio")
                                stage.touch()
                        }
                    }
                }
            }
        }
    }

    // ---- animation sync (collapsed: it is set once per speaker) ----
    Item {
        id: syncToggle
        x: 22
        y: presetFlow.y + presetFlow.height + 14
        width: parent.width - 44
        height: 28
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Animation sync")
            color: Glass.mute
            font.family: Glass.font
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(audio.syncMs) + " ms   " + (stage.syncOpen ? "⌃" : "⌄")
            color: Glass.faint
            font.family: Glass.font
            font.pixelSize: 12
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: stage.syncOpen = !stage.syncOpen }
    }

    // ---- animation delay: so the beat and the picture land together ----
    FxSlider {
        id: delay
        x: 22
        y: syncToggle.y + syncToggle.height + 6
        opacity: stage.syncOpen ? 1 : 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: 160 } }
        width: parent.width - 44
        label: audio.bluetooth ? qsTr("Animation delay (Bluetooth)") : qsTr("Animation delay")
        from: 0
        to: 500
        value: audio.syncMs
        unit: Math.round(audio.syncMs) + " ms"
        onMoved: function (v) { audio.syncMs = Math.round(v / 5) * 5 }
    }
    Text {
        x: 22
        y: delay.y + delay.height + 2
        width: parent.width - 44
        text: audio.bluetooth ? qsTr("Bluetooth plays sound a moment after the computer. Move this until the beat and the animation line up.")
            : qsTr("Holds the animation back to match your speakers. Remembered for each output.")
        id: delayHelp
        opacity: stage.syncOpen ? 1 : 0
        visible: opacity > 0.01
        color: Glass.faint
        wrapMode: Text.WordWrap
        font.family: Glass.font
        font.pixelSize: 11
    }

    // Tap to sync: tap the pad (or Space) on each kick you hear; the delay is set from the taps.
    Rectangle {
        id: tapPad
        x: 22
        opacity: stage.syncOpen ? 1 : 0
        visible: opacity > 0.01
        y: delayHelp.y + delayHelp.implicitHeight + 10
        width: parent.width - 44
        height: audio.tapping ? 64 : 36
        radius: 18
        color: audio.tapping ? Qt.rgba(Glass.accent.r, Glass.accent.g, Glass.accent.b, 0.25 + 0.35 * flash) : (tapArea.containsMouse ? Glass.fillHover : Glass.fill)
        border.color: audio.tapping ? Glass.accentLight : Glass.rimMiddle
        border.width: 1
        property real flash: 0
        scale: tapArea.pressed ? 0.96 : 1
        Behavior on scale { SpringAnimation { spring: Glass.springSnap; damping: Glass.dampingSnap; epsilon: 0.002 } }
        Behavior on height { SpringAnimation { spring: Glass.springPanel; damping: 0.4; epsilon: 0.1 } }
        NumberAnimation { id: flashAnim; target: tapPad; property: "flash"; from: 1; to: 0; duration: 260; easing.type: Easing.OutCubic }
        focus: audio.tapping
        Keys.onSpacePressed: { audio.tap(); flashAnim.restart() }
        Keys.onEscapePressed: audio.stopTapSync()
        Column {
            anchors.centerIn: parent
            spacing: 2
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: audio.tapping ? qsTr("Tap here on every kick") : qsTr("Tap to sync")
                color: Glass.ink
                font.family: Glass.font
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: text.length > 0
                text: audio.tapStatus
                width: Math.min(implicitWidth, tapPad.width - 24)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Glass.faint
                font.family: Glass.font
                font.pixelSize: 11
            }
        }
        MouseArea {
            id: tapArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onPressed: {
                if (audio.tapping) {
                    audio.tap()
                    flashAnim.restart()
                } else {
                    audio.startTapSync()
                    tapPad.forceActiveFocus()
                }
            }
        }
    }
}
