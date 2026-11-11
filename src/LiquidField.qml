import QtQuick

// Liquid light in the cover's colours (see shaders/liquid.frag): big soft pools that drift and merge, swell with
// their part of the spectrum, and brighten on each kick. Drawn at a quarter of the size and scaled up smoothly,
// which blurs it further and keeps it cheap at any screen size. Used behind the cinema view and inside its
// transport card.
Item {
    id: field

    property bool running: false
    property color primary: Glass.accent
    property color secondary: Glass.secondary
    property color ambient: Glass.ambient

    property real flow: 0
    property real ripple: 2
    property real lastKick: 0
    property vector4d bandsA: Qt.vector4d(0, 0, 0, 0)
    property vector4d bandsB: Qt.vector4d(0, 0, 0, 0)

    FrameAnimation {
        running: field.running && field.visible
        onTriggered: {
            const loud = Glass.reactive ? Glass.level : 0
            field.flow += frameTime * field.pace * (1 + 2.0 * loud)
            // A kick restarts the ring; it then travels outward over about two seconds.
            const kick = Glass.reactive ? Glass.kick : 0
            if (kick > 0.85 && field.lastKick <= 0.85)
                field.ripple = 0
            else
                field.ripple += frameTime * 0.85
            field.lastKick = kick
            // 24 bands down to 8: low to high, each the mean of three, eased so the waves do not jitter.
            const s = Glass.reactive ? audio.spectrum : []
            const out = []
            for (let i = 0; i < 8; ++i) {
                const v = s.length >= 24 ? (s[i * 3] + s[i * 3 + 1] + s[i * 3 + 2]) / 3 : 0
                const old = i < 4 ? [field.bandsA.x, field.bandsA.y, field.bandsA.z, field.bandsA.w][i]
                                  : [field.bandsB.x, field.bandsB.y, field.bandsB.z, field.bandsB.w][i - 4]
                // Instant rise, gentle fall.
                out.push(Math.max(v, old * Math.exp(-frameTime * 5)))
            }
            field.bandsA = Qt.vector4d(out[0], out[1], out[2], out[3])
            field.bandsB = Qt.vector4d(out[4], out[5], out[6], out[7])
        }
    }

    // Speed of the drift: slow, so it reads as a liquid, not a screensaver.
    property real pace: 0.35
    ShaderEffect {
        anchors.fill: parent
        layer.enabled: true
        layer.smooth: true
        layer.textureSize: Qt.size(Math.max(8, Math.ceil(width / 4)), Math.max(8, Math.ceil(height / 4)))
        property color primary: field.primary
        property color secondary: field.secondary
        property color ambient: field.ambient
        property vector4d bandsA: field.bandsA
        property vector4d bandsB: field.bandsB
        property size size: Qt.size(width, height)
        property real time: field.flow
        property real level: Glass.reactive ? Glass.level : 0
        property real bass: Glass.reactive ? Glass.bass : 0
        property real ripple: field.ripple
        property real kick: Glass.reactive ? Glass.kick : 0
        blending: true
        fragmentShader: Qt.resolvedUrl("shaders/liquid.frag.qsb")
    }
}
