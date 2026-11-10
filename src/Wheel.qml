import QtQuick

// Liquid ring. Drag the glass to move, tap a side for back, previous, next,
// or play, and tap the center to choose. Same jobs as the click wheel.
Item {
    id: wheel
    signal stepped(int steps)
    signal menuPressed()
    signal previousPressed()
    signal nextPressed()
    signal playPressed()
    signal centerPressed()

    property string pressedSector: ""
    property real turnAngle: -90
    property real phase: 0
    property bool dragging: false

    onPhaseChanged: glint.requestPaint()

    function sector(x, y) {
        const dx = x - width / 2
        const dy = y - height / 2
        const angle = Math.atan2(dy, dx)
        if (angle > -Math.PI * 0.75 && angle <= -Math.PI * 0.25) return "menu"
        if (angle > -Math.PI * 0.25 && angle <= Math.PI * 0.25) return "next"
        if (angle > Math.PI * 0.25 && angle <= Math.PI * 0.75) return "play"
        return "previous"
    }

    function fire(name) {
        if (name === "menu") menuPressed()
        else if (name === "previous") previousPressed()
        else if (name === "next") nextPressed()
        else if (name === "play") playPressed()
    }

    function turn(deltaY) {
        const steps = deltaY > 0 ? -1 : 1
        turnAngle += steps * 14
        stepped(steps)
    }

    Behavior on turnAngle {
        NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "#14f8e7dc"
        border.color: wheel.dragging ? "#a6ffd8bc" : "#3cf8e7dc"
        border.width: 1
        antialiasing: true
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width - 14
        height: width
        radius: width / 2
        color: "transparent"
        border.color: wheel.dragging ? "#73ffd8bc" : "#2ef8e7dc"
        border.width: 11
        antialiasing: true
    }

    Canvas {
        id: glint
        anchors.fill: parent
        z: 1
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const cx = width / 2
            const cy = height / 2
            const radius = width / 2 - 8
            const sweep = wheel.phase * 2 + wheel.turnAngle * Math.PI / 180
            ctx.beginPath()
            ctx.arc(cx, cy, radius, sweep, sweep + 0.85)
            ctx.strokeStyle = wheel.dragging ? "rgba(255,246,238,0.95)" : "rgba(255,246,238,0.72)"
            ctx.lineWidth = 3
            ctx.lineCap = "round"
            ctx.stroke()
            ctx.beginPath()
            ctx.arc(cx, cy, radius, sweep + 2.4, sweep + 2.75)
            ctx.strokeStyle = "rgba(232,137,154,0.8)"
            ctx.lineWidth = 2
            ctx.stroke()
        }
    }

    Item {
        anchors.fill: parent
        rotation: wheel.turnAngle
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: 5
            width: 4
            height: 16
            radius: 2
            color: "#fff6ee"
            opacity: wheel.dragging ? 1 : 0.8
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.07
        text: "⌃"
        color: wheel.pressedSector === "menu" ? "#fff6ee" : "#a6f8e7dc"
        font.family: "Inter"
        font.pixelSize: 13
        font.weight: Font.Medium
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        x: parent.width * 0.055
        text: "◀"
        color: wheel.pressedSector === "previous" ? "#fff6ee" : "#c8f8e7dc"
        font.family: "Inter"
        font.pixelSize: 11
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: parent.width * 0.055
        text: "▶"
        color: wheel.pressedSector === "next" ? "#fff6ee" : "#c8f8e7dc"
        font.family: "Inter"
        font.pixelSize: 11
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: parent.height * 0.055
        text: backend.playing ? "❚❚" : "▶"
        color: wheel.pressedSector === "play" ? "#fff6ee" : "#a6f8e7dc"
        font.family: "Inter"
        font.pixelSize: 9
        font.letterSpacing: 1
    }

    MouseArea {
        id: ring
        z: 2
        anchors.fill: parent
        preventStealing: true
        property real last: 0
        property real carried: 0
        property bool dragged: false

        onPressed: function (mouse) {
            const dx = mouse.x - width / 2
            const dy = mouse.y - height / 2
            if (Math.hypot(dx, dy) < width * 0.28) {
                mouse.accepted = false
                return
            }
            last = Math.atan2(dy, dx)
            carried = 0
            dragged = false
            wheel.dragging = true
            wheel.pressedSector = wheel.sector(mouse.x, mouse.y)
        }
        onPositionChanged: function (mouse) {
            if (!pressed) return
            const dx = mouse.x - width / 2
            const dy = mouse.y - height / 2
            if (Math.hypot(dx, dy) < width * 0.28) return
            let delta = Math.atan2(dy, dx) - last
            if (delta > Math.PI) delta -= Math.PI * 2
            if (delta < -Math.PI) delta += Math.PI * 2
            last = Math.atan2(dy, dx)
            carried += delta
            if (Math.abs(carried) > 0.08) dragged = true
            const notch = 0.32
            while (carried <= -notch) {
                carried += notch
                wheel.turnAngle += 14
                wheel.stepped(1)
            }
            while (carried >= notch) {
                carried -= notch
                wheel.turnAngle -= 14
                wheel.stepped(-1)
            }
        }
        onReleased: function (mouse) {
            const dx = mouse.x - width / 2
            const dy = mouse.y - height / 2
            if (!dragged && Math.hypot(dx, dy) >= width * 0.28)
                wheel.fire(wheel.sector(mouse.x, mouse.y))
            wheel.pressedSector = ""
            wheel.dragging = false
            dragged = false
        }
        onCanceled: {
            wheel.pressedSector = ""
            wheel.dragging = false
            dragged = false
        }
        onWheel: function (wheelEvent) { wheel.turn(wheelEvent.angleDelta.y) }
    }

    Rectangle {
        id: halo
        anchors.centerIn: center
        width: center.width + 16
        height: width
        radius: width / 2
        color: "transparent"
        border.color: "#e8899a"
        border.width: 1
        opacity: 0
        visible: backend.playing
        antialiasing: true

        SequentialAnimation on opacity {
            running: backend.playing
            loops: Animation.Infinite
            NumberAnimation { from: 0.2; to: 0.7; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.7; to: 0.2; duration: 900; easing.type: Easing.InOutSine }
        }
    }

    Rectangle {
        id: center
        z: 3
        anchors.centerIn: parent
        width: parent.width * 0.56
        height: width
        radius: width / 2
        color: centerArea.pressed ? "#2214110c" : "#28f8e7dc"
        border.color: "#73fff6ee"
        border.width: 1
        antialiasing: true

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: parent.height * 0.08
            width: parent.width * 0.62
            height: parent.height * 0.28
            radius: width / 2
            color: "#fff6ee"
            opacity: 0.16
        }

        Text {
            anchors.centerIn: parent
            text: backend.kind === "now" ? (backend.playing ? "❚❚" : "▶") : "●"
            color: "#fff6ee"
            font.family: "Inter Display"
            font.pixelSize: backend.kind === "now" ? 16 : 10
            font.weight: Font.Medium
        }

        MouseArea {
            id: centerArea
            anchors.fill: parent
            onClicked: wheel.centerPressed()
            onWheel: function (wheelEvent) { wheel.turn(wheelEvent.angleDelta.y) }
        }
    }
}
