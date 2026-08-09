import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property QtObject appController
    title: "设置"
    modal: true
    anchors.centerIn: parent
    width: Math.min(460, parent ? parent.width - 48 : 460)
    standardButtons: Dialog.Close
    ShareMeTheme { id: theme }
    background: Rectangle { color: theme.surface; radius: 14; border.color: theme.border }
    contentItem: ColumnLayout {
        spacing: 12
        Text { text: "连接"; color: theme.textPrimary; font.pixelSize: 16; font.weight: Font.Bold }
        Text { text: "开发服务地址"; color: theme.textSecondary; font.pixelSize: 12 }
        TextField {
            Layout.fillWidth: true
            text: dialog.appController.serverUrl
            Accessible.name: "WebSocket 服务地址"
            onEditingFinished: dialog.appController.serverUrl = text
        }
        Text {
            Layout.fillWidth: true
            text: "服务地址只在当前运行中使用，不会和房间码一起保存。设备切换将在底层支持安全热切换后启用。"
            color: theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
