import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtMultimedia

ApplicationWindow {
    id: window
    required property PlaybackController playback

    width: 1120
    height: 720
    minimumWidth: 760
    minimumHeight: 500
    visible: true
    color: "#0b0a09"
    title: "ShareMe · Local Playback Proof"

    FileDialog {
        id: movieDialog
        title: "Choose a local movie"
        nameFilters: [
            "Movie files (*.mp4 *.mkv *.mov *.m4v *.avi *.webm)",
            "All files (*)"
        ]
        onAccepted: window.playback.open(selectedFile)
    }

    Rectangle {
        anchors.fill: parent
        color: "#0b0a09"

        Rectangle {
            id: topRail
            anchors.left: parent.left
            anchors.right: parent.right
            height: 54
            color: "#11100e"
            border.color: "#27241f"

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Rectangle {
                    width: 7
                    height: 7
                    radius: 4
                    color: window.playback.state === "playing" ?
                               "#d69b49" : "#5a554d"
                }

                Text {
                    text: "SHAREME / LOCAL PROJECTION"
                    color: "#e7dfd1"
                    font.family: Qt.platform.os === "windows" ? "Bahnschrift" :
                                                                "Avenir Next Condensed"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2.2
                }
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                text: window.playback.state.toUpperCase()
                color: "#7f786d"
                font.family: "Menlo"
                font.pixelSize: 10
                font.letterSpacing: 1.4
            }
        }

        Rectangle {
            id: stage
            anchors.top: topRail.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: controls.top
            anchors.margins: 18
            color: "#050505"
            border.color: "#28251f"
            border.width: 1
            radius: 7
            clip: true

            VideoOutput {
                id: videoOutput
                anchors.fill: parent
                anchors.margins: 1
                fillMode: VideoOutput.PreserveAspectFit
                Component.onCompleted: window.playback.setVideoSink(videoSink)
            }

            Column {
                anchors.centerIn: parent
                spacing: 12
                visible: window.playback.state === "closed"

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "NO REEL LOADED"
                    color: "#766e62"
                    font.family: Qt.platform.os === "windows" ? "Bahnschrift" :
                                                                "Avenir Next Condensed"
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                    font.letterSpacing: 3
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Open a local movie to validate the direct media path."
                    color: "#4f4a43"
                    font.pixelSize: 12
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: errorText.implicitHeight + 22
                color: "#b3261e"
                visible: window.playback.errorMessage.length > 0

                Text {
                    id: errorText
                    anchors.fill: parent
                    anchors.margins: 11
                    text: window.playback.errorMessage
                    color: "#fff3ed"
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
            }
        }

        PlaybackControls {
            id: controls
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            anchors.bottomMargin: 18
            playbackState: window.playback.state
            positionMs: window.playback.positionMs
            durationMs: window.playback.durationMs
            onOpenRequested: movieDialog.open()
            onPlayRequested: window.playback.play()
            onPauseRequested: window.playback.pause()
            onSeekRequested: position => window.playback.seek(position)
        }
    }
}
