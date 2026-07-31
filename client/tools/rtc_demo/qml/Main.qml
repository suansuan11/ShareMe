import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia

ApplicationWindow {
    id: window
    required property QtObject controller
    width: 960
    height: 600
    visible: true
    color: "#111318"
    title: controller.viewer ? "ShareMe Receiver" : "ShareMe Sender"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: window.controller.viewer ? "Receiver" : "Sender"
                color: "white"
                font.pixelSize: 22
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Label {
                text: window.controller.roomId.length > 0
                      ? "Room " + window.controller.roomId
                      : "Room pending"
                color: "#8bd5ff"
                font.pixelSize: 18
                font.bold: true
            }
        }

        Label {
            Layout.fillWidth: true
            text: "Status: " + window.controller.status
            color: "#c7ccd8"
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"
            radius: 8

            VideoOutput {
                anchors.fill: parent
                anchors.margins: 2
                fillMode: VideoOutput.PreserveAspectFit
                Component.onCompleted: window.controller.setVideoSink(videoSink)
            }

            Label {
                anchors.centerIn: parent
                visible: window.controller.status !== "connected"
                text: window.controller.viewer
                      ? "Waiting for remote video"
                      : "Share this room code with the viewer"
                color: "#7f8798"
                font.pixelSize: 18
            }
        }
    }
}
