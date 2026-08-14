import QtQuick
import QtQuick.Controls
import QtMultimedia

Item {
    id: stage
    required property var controller
    property bool captureRecovering: controller.status.startsWith("screen-capture-recovering:")
    property bool sessionTransition: controller.status.startsWith("session-suspended:")
                                     || controller.status === "session-resuming"
    ShareMeTheme { id: theme }

    Rectangle {
        anchors.fill: parent
        color: theme.background
    }

    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
        Component.onCompleted: stage.controller.setVideoSink(videoSink)
    }

    Row {
        id: statusRow
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 7
        visible: !stage.sessionTransition
                 && (stage.controller.status === "connected"
                  || stage.captureRecovering
                  || stage.controller.remoteVideoAvailable)
        Rectangle { width: 6; height: 6; radius: 3; color: stage.captureRecovering ? theme.warning : theme.success }
        Text {
            text: stage.captureRecovering ? "正在恢复屏幕共享"
                                          : stage.controller.viewer ? "正在接收屏幕" : "正在共享屏幕"
            color: stage.captureRecovering ? theme.warning : theme.textSecondary
            font.pixelSize: 11
        }
    }

    Column {
        id: stageMessage
        anchors.centerIn: parent
        spacing: 6
        visible: stage.captureRecovering
                 || (stage.controller.viewer
                     ? !stage.controller.remoteVideoAvailable
                     : stage.controller.status !== "connected"
                       && !stage.sessionTransition)
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: stage.captureRecovering ? "正在恢复屏幕共享"
                                          : stage.controller.viewer ? "正在等待屏幕共享…" : "正在准备屏幕共享…"
            color: theme.textPrimary
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: stage.captureRecovering
            text: "语音连接保持工作，画面会自动恢复"
            color: theme.textMuted
            font.pixelSize: 11
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        visible: stage.sessionTransition
        text: stage.controller.status === "session-resuming"
              ? "正在恢复通话"
              : "语音与画面连接会在系统恢复后检查"
        color: theme.textSecondary
        font.pixelSize: 11
    }
}
