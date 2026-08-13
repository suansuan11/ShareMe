import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    required property var controller
    property bool sessionSuspended: controller.status.startsWith("session-suspended:")
    property bool sessionResuming: controller.status === "session-resuming"
    property string connectionLabel: sessionSuspended ? "系统暂停，等待恢复"
                                     : sessionResuming ? "正在恢复通话"
                                     : controller.status.startsWith("screen-capture-recovering:")
                                     ? "正在恢复屏幕共享"
                                     : controller.status === "connected" ? "连接稳定"
                                     : controller.status === "negotiating" ? "正在建立媒体连接"
                                     : controller.status === "signaling" ? "正在连接房间"
                                     : controller.status === "ended" ? "通话已结束"
                                     : "连接中"
    signal roomCopied()
    implicitHeight: 58
    color: theme.surface
    border.width: 0
    ShareMeTheme { id: theme }

    function formattedRoom(room) {
        return room && room.length === 6
                ? room.substring(0, 3) + "-" + room.substring(3) : room
    }

    TextInput {
        id: clipboardSource
        visible: false
        text: bar.controller.roomId
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 14
        Text {
            text: "ShareMe"
            color: theme.textPrimary
            font.pixelSize: 18
            font.weight: Font.Bold
        }
        Item { Layout.fillWidth: true }
        Rectangle {
            implicitWidth: connectionRow.implicitWidth + 20
            implicitHeight: 30
            radius: 15
            color: bar.sessionSuspended || bar.sessionResuming ? "#2D2512" : "#10251F"
            RowLayout {
                id: connectionRow
                anchors.centerIn: parent
                spacing: 7
                Rectangle {
                    width: 7; height: 7; radius: 4
                    color: bar.sessionSuspended || bar.sessionResuming ? "#F2C94C" : theme.healthy
                }
                Text {
                    text: bar.connectionLabel
                    color: bar.sessionSuspended || bar.sessionResuming ? "#F2C94C" : theme.healthy
                    font.pixelSize: 11
                }
            }
        }
        Button {
            id: roomButton
            text: bar.controller.roomId.length > 0
                  ? "房间 " + bar.formattedRoom(bar.controller.roomId) + "  复制"
                  : "正在创建房间"
            enabled: bar.controller.roomId.length > 0
            Accessible.name: enabled ? "复制房间码 " + bar.formattedRoom(bar.controller.roomId)
                                     : "正在创建房间"
            onClicked: {
                clipboardSource.selectAll()
                clipboardSource.copy()
                bar.roomCopied()
            }
            contentItem: Text {
                text: parent.text
                color: parent.enabled ? theme.cyan : theme.textMuted
                font.pixelSize: 11
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 15
                color: roomButton.hovered ? "#18384F" : "#10283D"
                border.width: 1
                border.color: "#1B4A68"
            }
        }
    }
}
