import QtQuick

// Ctrl+K: type a few letters to run anything. Matching is fuzzy (see
// commands.h). Whatever you type can also be sent straight to an Apple Music
// search, or to Grok as a mix ("mix: synthwave rainy night").
Item {
    id: commandPalette

    property bool open: false
    property Item source: null
    // The commands on offer: { id, label, hint, keywords, arg }.
    property var items: []
    signal triggered(string id, string arg)
    signal dismissed()

    readonly property alias query: input.text
    property int selected: 0
    property var results: []

    // A leading "mix:" or "make mix:" means the rest is a mood for Grok.
    function mood(text) {
        const match = text.match(/^\s*(?:make\s+(?:a\s+)?)?mix\s*:?\s+(.*)$/i)
        return match ? match[1].trim() : ""
    }

    function refresh() {
        const text = input.text.trim()
        const out = commands.rank(text, items, 7)
        if (text.length > 0) {
            const asked = mood(text)
            const typed = []
            if (backend.mixAvailable)
                typed.push({ id: "mix", label: qsTr("Make a mix: “%1”").arg(asked || text), hint: "Grok", arg: asked || text })
            typed.push({ id: "search", label: qsTr("Search Apple Music for “%1”").arg(text), hint: "", arg: text })
            // Asking for a mix by name puts it on top; otherwise typed text comes after real commands.
            const first = asked !== "" && backend.mixAvailable ? [typed.shift()] : []
            // A described sound ("muffled club bathroom") becomes an EQ setting, ahead of the fuzzy matches
            // unless a command's own name was typed exactly ("bass boost" is already a command).
            const sound = asked === "" && effects.available ? commands.describeSound(text) : ({})
            const vibe = []
            if (sound.summary !== undefined) {
                const exact = out.length > 0 && out[0].label.toLowerCase() === text.toLowerCase()
                const item = { id: "vibe", label: qsTr("Sound like: %1").arg(sound.summary), hint: "EQ", arg: JSON.stringify(sound) }
                // Only a phrase made wholly of sound words leads ("night drive" is a song, not a mood).
                if (sound.complete && !exact) vibe.push(item); else typed.push(item)
            }
            results = first.concat(vibe, out, typed).slice(0, 7)
        } else {
            results = out
        }
        selected = Math.min(selected, Math.max(0, results.length - 1))
    }

    function run(index) {
        const item = results[index]
        if (!item)
            return
        commandPalette.triggered(item.id, item.arg || "")
    }

    onOpenChanged: {
        if (open) {
            input.text = ""
            selected = 0
            refresh()
            input.forceActiveFocus()
        }
    }
    onItemsChanged: if (open) refresh()

    anchors.fill: parent
    visible: scrim.opacity > 0.01

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Glass.scrim
        opacity: commandPalette.open ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        MouseArea { anchors.fill: parent; onClicked: commandPalette.dismissed() }
    }

    GlassPanel {
        id: box
        source: commandPalette.source
        x: 12
        y: 64
        width: parent.width - 24
        height: 58 + Math.max(1, commandPalette.results.length) * 44 + 10
        Behavior on height { SpringAnimation { spring: Glass.springPanel; damping: 0.4 } }
        radius: 22
        opacity: commandPalette.open ? 1 : 0
        scale: commandPalette.open ? 1 : 0.9
        transformOrigin: Item.Top
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Behavior on scale { SpringAnimation { spring: Glass.springPanel; damping: Glass.dampingPanel; epsilon: 0.002 } }

        MouseArea { anchors.fill: parent }

        Glyph {
            x: 18
            y: 19
            size: 20
            color: Glass.faint
            path: "M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z"
        }
        Text {
            x: 48
            y: 0
            height: 58
            verticalAlignment: Text.AlignVCenter
            visible: input.text.length === 0
            text: qsTr("Type a command, a song, or “mix: …”")
            color: Glass.faint
            font.family: Glass.font
            font.pixelSize: 15
        }
        TextInput {
            id: input
            x: 48
            y: 0
            width: parent.width - 64
            height: 58
            verticalAlignment: TextInput.AlignVCenter
            color: Glass.ink
            font.family: Glass.font
            font.pixelSize: 15
            clip: true
            selectByMouse: true
            maximumLength: 200
            onTextChanged: { commandPalette.selected = 0; commandPalette.refresh() }
            Keys.onUpPressed: commandPalette.selected = (commandPalette.selected - 1 + commandPalette.results.length) % Math.max(1, commandPalette.results.length)
            Keys.onDownPressed: commandPalette.selected = (commandPalette.selected + 1) % Math.max(1, commandPalette.results.length)
            Keys.onReturnPressed: commandPalette.run(commandPalette.selected)
            Keys.onEnterPressed: commandPalette.run(commandPalette.selected)
            Keys.onEscapePressed: commandPalette.dismissed()
        }
        Rectangle {
            x: 14
            y: 57
            width: parent.width - 28
            height: 1
            color: Glass.specular
        }
        Text {
            visible: commandPalette.results.length === 0
            x: 18
            y: 58
            height: 44
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Nothing matches")
            color: Glass.faint
            font.family: Glass.font
            font.pixelSize: 14
        }
        Column {
            x: 8
            y: 62
            width: parent.width - 16
            Repeater {
                model: commandPalette.results
                Item {
                    id: row
                    required property int index
                    required property var modelData
                    readonly property bool current: index === commandPalette.selected
                    width: parent.width
                    height: 44
                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 2
                        anchors.bottomMargin: 2
                        radius: 12
                        color: "#30ffffff"
                        border.color: Glass.specular
                        border.width: 1
                        opacity: row.current ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 90 } }
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.right: hint.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
                        color: Glass.ink
                        elide: Text.ElideRight
                        font.family: Glass.font
                        font.pixelSize: 14
                        font.weight: row.current ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        id: hint
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.hint || ""
                        color: Glass.faint
                        font.family: Glass.font
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: commandPalette.selected = row.index
                        onClicked: commandPalette.run(row.index)
                    }
                }
            }
        }
    }
}
