import QtQuick
import QtQuick.Controls
import QtMultimedia

Rectangle {
    id: stage
    required property var controller
    radius: 14
    color: "#03060A"
    border.width: 1
    border.color: theme.border
    clip: true
    ShareMeTheme { id: theme }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        anchors.margins: 2
        fillMode: VideoOutput.PreserveAspectFit
        Component.onCompleted: stage.controller.setVideoSink(videoSink)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 14
        implicitWidth: liveRow.implicitWidth + 18
        implicitHeight: 28
        radius: 8
        color: "#132A23"
        visible: stage.controller.status === "connected"
                 || stage.controller.remoteVideoAvailable
        Row {
            id: liveRow
            anchors.centerIn: parent
            spacing: 7
            Rectangle { width: 7; height: 7; radius: 4; color: theme.healthy }
            Text {
                text: stage.controller.viewer ? "正在接收屏幕" : "正在共享屏幕"
                color: theme.healthy
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: stage.controller.viewer
                 ? !stage.controller.remoteVideoAvailable
                 : stage.controller.status !== "connected"
        Rectangle {
            width: 64; height: 64; radius: 20
            anchors.horizontalCenter: parent.horizontalCenter
            color: theme.surfaceRaised
            border.width: 1
            border.color: theme.border
            Text {
                anchors.centerIn: parent
                text: stage.controller.viewer ? "▣" : "↗"
                color: theme.cyan
                font.pixelSize: 27
                font.weight: Font.Bold
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: stage.controller.viewer ? "等待主持人共享屏幕" : "正在准备屏幕共享"
            color: theme.textPrimary
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: stage.controller.viewer ? "语音连接会保持独立工作" : "房间创建后即可邀请另一位参与者"
            color: theme.textMuted
            font.pixelSize: 11
        }
    }
}
