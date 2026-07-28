import QtQuick
import QtQuick.Controls

Item {
    id: root

    property string playbackState: "closed"
    property real positionMs: 0
    property real durationMs: 0
    signal openRequested()
    signal playRequested()
    signal pauseRequested()
    signal seekRequested(real positionMs)

    implicitHeight: 92

    function formatTime(valueMs) {
        const total = Math.max(0, Math.floor(valueMs / 1000))
        const hours = Math.floor(total / 3600)
        const minutes = Math.floor((total % 3600) / 60)
        const seconds = total % 60
        if (hours > 0)
            return hours + ":" + String(minutes).padStart(2, "0") + ":" +
                    String(seconds).padStart(2, "0")
        return String(minutes).padStart(2, "0") + ":" +
                String(seconds).padStart(2, "0")
    }

    Rectangle {
        anchors.fill: parent
        color: "#141311"
        border.color: "#302d27"
        border.width: 1
        radius: 8
    }

    Row {
        id: actionRow
        anchors.left: parent.left
        anchors.leftMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        Button {
            id: openButton
            text: "OPEN"
            onClicked: root.openRequested()

            contentItem: Text {
                text: openButton.text
                color: "#f2eadb"
                font.family: Qt.platform.os === "windows" ? "Bahnschrift" :
                                                            "Avenir Next Condensed"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.letterSpacing: 1.5
            }
            background: Rectangle {
                color: openButton.down ? "#3b362e" : "#24211d"
                border.color: openButton.hovered ? "#d69b49" : "#4a443a"
                radius: 4
            }
        }

        Button {
            id: playButton
            enabled: root.playbackState === "paused"
            text: "PLAY"
            onClicked: root.playRequested()

            contentItem: Text {
                text: playButton.text
                color: playButton.enabled ? "#15120d" : "#6b655c"
                font.family: Qt.platform.os === "windows" ? "Bahnschrift" :
                                                            "Avenir Next Condensed"
                font.pixelSize: 12
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.letterSpacing: 1.5
            }
            background: Rectangle {
                color: playButton.enabled ?
                           (playButton.down ? "#b8782f" : "#d69b49") :
                           "#26231f"
                radius: 4
            }
        }

        Button {
            id: pauseButton
            enabled: root.playbackState === "playing"
            text: "PAUSE"
            onClicked: root.pauseRequested()

            contentItem: Text {
                text: pauseButton.text
                color: pauseButton.enabled ? "#f2eadb" : "#6b655c"
                font.family: Qt.platform.os === "windows" ? "Bahnschrift" :
                                                            "Avenir Next Condensed"
                font.pixelSize: 12
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.letterSpacing: 1.5
            }
            background: Rectangle {
                color: pauseButton.down ? "#3b362e" : "#24211d"
                border.color: pauseButton.enabled ? "#665d50" : "#302d27"
                radius: 4
            }
        }
    }

    Column {
        anchors.left: actionRow.right
        anchors.leftMargin: 22
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7

        Row {
            width: parent.width

            Text {
                text: root.formatTime(root.positionMs)
                color: "#d69b49"
                font.family: "Menlo"
                font.pixelSize: 11
            }
            Item { width: parent.width - 88; height: 1 }
            Text {
                text: root.formatTime(root.durationMs)
                color: "#8f887c"
                font.family: "Menlo"
                font.pixelSize: 11
            }
        }

        Slider {
            id: timeline
            width: parent.width
            from: 0
            to: Math.max(1, root.durationMs)
            value: root.positionMs
            enabled: root.playbackState !== "closed" &&
                     root.playbackState !== "failed"
            onPressedChanged: {
                if (!pressed && enabled)
                    root.seekRequested(value)
            }

            background: Rectangle {
                x: timeline.leftPadding
                y: timeline.topPadding + timeline.availableHeight / 2 - 1
                width: timeline.availableWidth
                height: 2
                color: "#403b33"

                Rectangle {
                    width: timeline.visualPosition * parent.width
                    height: parent.height
                    color: "#d69b49"
                }
            }
            handle: Rectangle {
                x: timeline.leftPadding +
                   timeline.visualPosition *
                   (timeline.availableWidth - width)
                y: timeline.topPadding +
                   timeline.availableHeight / 2 - height / 2
                width: timeline.pressed ? 14 : 10
                height: width
                radius: width / 2
                color: "#f3d29e"
                border.color: "#6f4b22"
            }
        }
    }
}
