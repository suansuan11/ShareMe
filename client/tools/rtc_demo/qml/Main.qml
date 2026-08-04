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

        Label {
            Layout.fillWidth: true
            visible: window.controller.viewer
                     && window.controller.viewerRenderedAvailable
            text: "Rendered movie: "
                  + window.controller.viewerRenderedPositionMs + " ms"
            color: "#9fd7b5"
        }

        Label {
            Layout.fillWidth: true
            visible: !window.controller.viewer
                     && window.controller.viewerRenderedAvailable
            text: "Viewer playout: "
                  + window.controller.viewerRenderedPositionMs + " ms · delta "
                  + window.controller.hostViewerDeltaMs + " ms · "
                  + window.controller.hostSyncAction
            color: "#9fd7b5"
        }

        Label {
            Layout.fillWidth: true
            visible: window.controller.viewer
            text: "Host playback: " + window.controller.remotePlaybackState
                  + " · " + window.controller.remotePlaybackPositionMs + " ms"
            color: "#8bd5ff"
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: !window.controller.viewer
                     && window.controller.hostControlsAvailable
            spacing: 6

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: window.controller.hostPlaybackState === "playing"
                          ? "Pause" : "Resume"
                    onClicked: {
                        if (window.controller.hostPlaybackState === "playing")
                            window.controller.pauseHostPlayback()
                        else
                            window.controller.resumeHostPlayback()
                    }
                }

                Label {
                    text: Math.max(0, window.controller.hostPlaybackPositionMs
                                      - window.controller.hostPlaybackStartMs)
                          + " / " + window.controller.hostPlaybackDurationMs
                          + " ms · generation "
                          + window.controller.hostPlaybackGeneration
                    color: "#c7ccd8"
                }
            }

            Slider {
                id: playbackSlider
                Layout.fillWidth: true
                from: 0
                to: Math.max(0, window.controller.hostPlaybackDurationMs)
                stepSize: 10
                onPressedChanged: {
                    if (!pressed)
                        window.controller.seekHostPlayback(
                            window.controller.hostPlaybackStartMs
                            + Math.round(value))
                }

                Binding {
                    target: playbackSlider
                    property: "value"
                    when: !playbackSlider.pressed
                    value: Math.max(
                        playbackSlider.from,
                        Math.min(playbackSlider.to,
                                 window.controller.hostPlaybackPositionMs
                                 - window.controller.hostPlaybackStartMs))
                }
            }
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
