import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    visibility: Window.Maximized
    flags: Qt.Window | Qt.WindowStaysOnTopHint
    color: "#101010"
    title: "ShareMe Screen Motion Fixture"
    Accessible.name: "ShareMe deterministic screen motion fixture"

    property int renderedFrames: 0
    property real phase: 0

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        height: parent.height * 0.32
        Repeater {
            model: ["#ffffff", "#ffff00", "#00ffff", "#00ff00",
                    "#ff00ff", "#ff0000", "#0000ff", "#000000"]
            Rectangle {
                required property var modelData
                width: root.width / 8
                height: parent.height
                color: modelData
            }
        }
    }

    Canvas {
        id: patterns
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: parent.height * 0.32
        height: parent.height * 0.28
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = "#20242c"
            ctx.fillRect(0, 0, width, height)
            for (let x = 0; x < width / 2; x += 2) {
                ctx.fillStyle = x % 4 === 0 ? "white" : "black"
                ctx.fillRect(x, 0, 1, height)
            }
            for (let y = 0; y < height; y += 4) {
                ctx.fillStyle = y % 8 === 0 ? "#f0f0f0" : "#101010"
                ctx.fillRect(width / 2, y, width / 2, 2)
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: parent.height * 0.40
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "#000000" }
            GradientStop { position: 0.5; color: "#808080" }
            GradientStop { position: 1.0; color: "#ffffff" }
        }
    }

    Rectangle {
        width: Math.max(64, root.width * 0.08)
        height: width
        radius: 8
        color: "#ff7a45"
        border.width: 2
        border.color: "white"
        x: (root.width - width) * root.phase
        y: root.height * 0.66
    }

    Text {
        anchors.centerIn: parent
        text: "ShareMe motion / 细线与文字\n" + fixtureProfile
              + "  frame=" + root.renderedFrames
        color: "#ffffff"
        font.pixelSize: Math.max(22, root.height * 0.045)
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        style: Text.Outline
        styleColor: "#000000"
    }

    Timer {
        interval: Math.max(1, Math.round(1000 / fixtureFps))
        running: true
        repeat: true
        onTriggered: {
            root.renderedFrames += 1
            root.phase = (root.renderedFrames % (fixtureFps * 4))
                         / (fixtureFps * 4)
        }
    }
}
