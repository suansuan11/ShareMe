import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    required property var controller
    property bool sessionSuspended: controller.status.startsWith("session-suspended:")
    property bool sessionResuming: controller.status === "session-resuming"
    property bool screenRecovering: controller.status.startsWith("screen-capture-recovering:")
    property bool connectionHealthy: controller.status === "connected"
    property string connectionLabel: screenRecovering ? "正在恢复屏幕共享"
                                     : sessionSuspended ? "系统暂停，等待恢复"
                                     : sessionResuming ? "正在恢复通话"
                                     : connectionHealthy ? "连接稳定"
                                     : controller.status === "negotiating" ? "正在连接"
                                     : controller.status === "signaling" ? "正在连接房间"
                                     : controller.status === "ended" ? "通话已结束"
                                     : "连接中"
    property color connectionColor: connectionHealthy ? theme.success
                                    : controller.status === "ended" ? theme.textMuted
                                    : theme.warning
    signal roomCopied()
    implicitHeight: 58
    height: implicitHeight
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
            implicitWidth: connectionRow.implicitWidth + 16
            implicitHeight: 30
            radius: 15
            color: bar.connectionHealthy ? theme.successSurface
                  : bar.controller.status === "ended" ? theme.surfaceRaised
                  : theme.warningSurface
            RowLayout {
                id: connectionRow
                anchors.centerIn: parent
                spacing: 7
                Rectangle {
                    width: 7; height: 7; radius: 4
                    color: bar.connectionColor
                }
                Text {
                    text: bar.connectionLabel
                    color: bar.connectionColor
                    font.pixelSize: 11
                }
            }
        }
        Button {
            id: roomButton
            text: bar.controller.roomId.length > 0
                  ? bar.formattedRoom(bar.controller.roomId) : "正在创建房间"
            enabled: bar.controller.roomId.length > 0
            property color copyColor: !enabled ? theme.textDisabled
                                         : down ? theme.accentPressed
                                         : hovered ? theme.accentHover : theme.accent
            implicitHeight: theme.controlHeight
            Layout.minimumHeight: theme.controlHeight
            hoverEnabled: true
            Accessible.name: enabled ? "复制房间码 " + text
                                     : "正在创建房间"
            Accessible.description: enabled ? "复制当前房间码" : "房间码正在创建"
            ToolTip.text: enabled ? "复制房间码" : "正在创建房间"
            ToolTip.visible: (hovered || activeFocus) && ToolTip.text.length > 0
            focusPolicy: Qt.StrongFocus
            padding: 9
            leftPadding: 12
            rightPadding: 12
            onClicked: {
                clipboardSource.selectAll()
                clipboardSource.copy()
                bar.roomCopied()
            }
            contentItem: RowLayout {
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: roomButton.text
                    color: roomButton.copyColor
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                IconGlyph {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    name: "copy"
                    size: 16
                    color: roomButton.copyColor
                }
            }
            background: Rectangle {
                radius: 15
                color: !roomButton.enabled ? theme.surfaceDisabled
                      : roomButton.down ? theme.surfacePressed
                      : roomButton.hovered ? theme.surfaceHover : theme.accentSubtle
                border.width: roomButton.activeFocus ? 2 : 1
                border.color: roomButton.activeFocus ? theme.focus
                              : !roomButton.enabled ? theme.border
                              : roomButton.down ? theme.accentPressed
                              : roomButton.hovered ? theme.accentHover
                              : theme.borderStrong
            }
        }
    }
}
