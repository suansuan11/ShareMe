import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    title: "使用帮助"
    modal: true
    anchors.centerIn: parent
    width: Math.min(500, parent ? parent.width - 48 : 500)
    standardButtons: Dialog.Close
    ShareMeTheme { id: theme }
    background: Rectangle { color: theme.surface; radius: 14; border.color: theme.border }
    contentItem: ColumnLayout {
        spacing: 12
        Text { text: "三步开始共享"; color: theme.textPrimary; font.pixelSize: 17; font.weight: Font.Bold }
        Text {
            Layout.fillWidth: true
            text: "1. 主持人创建房间并允许屏幕与麦克风权限。\n2. 将六位房间码发给另一位参与者。\n3. 对方加入后即可观看屏幕并进行语音通话。"
            color: theme.textSecondary
            font.pixelSize: 12
            lineHeight: 1.45
            wrapMode: Text.WordWrap
        }
        Text { text: "键盘操作"; color: theme.textPrimary; font.pixelSize: 14; font.weight: Font.DemiBold }
        Text {
            text: "Tab：移动焦点　　Enter / Space：执行按钮　　Esc：关闭对话框"
            color: theme.textSecondary
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
}
