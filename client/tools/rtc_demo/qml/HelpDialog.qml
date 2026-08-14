import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogSurface {
    id: dialog
    objectName: "helpDialog"
    title: "使用帮助"
    closeAccessibleDescription: "关闭帮助"
    ShareMeTheme { id: theme }

    contentItem: ColumnLayout {
        spacing: theme.spacingSm
        Text {
            text: "怎么创建房间？"
            color: theme.textPrimary
            font.pixelSize: theme.fontLabel
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "选择创建房间，确认屏幕和麦克风权限后开始共享，再把六位房间码发给参与者。"
            color: theme.textSecondary
            font.pixelSize: theme.fontMeta
            wrapMode: Text.WordWrap
        }

        Text {
            text: "怎么加入房间？"
            color: theme.textPrimary
            font.pixelSize: theme.fontLabel
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "选择加入房间，输入对方提供的六位房间码并确认，即可观看屏幕和进行语音通话。"
            color: theme.textSecondary
            font.pixelSize: theme.fontMeta
            wrapMode: Text.WordWrap
        }

        Text {
            text: "没有画面怎么办？"
            color: theme.textPrimary
            font.pixelSize: theme.fontLabel
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "请确认主持人仍在共享，并检查 ShareMe 的屏幕录制权限；恢复期间请稍等片刻。"
            color: theme.textSecondary
            font.pixelSize: theme.fontMeta
            wrapMode: Text.WordWrap
        }

        Text {
            text: "没有声音怎么办？"
            color: theme.textPrimary
            font.pixelSize: theme.fontLabel
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "请检查通话中的麦克风、扬声器开关和系统音量，并确认 ShareMe 已获得麦克风权限。"
            color: theme.textSecondary
            font.pixelSize: theme.fontMeta
            wrapMode: Text.WordWrap
        }

        Text {
            text: "macOS 屏幕录制权限在哪里？"
            color: theme.textPrimary
            font.pixelSize: theme.fontLabel
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "在 macOS 的隐私与安全性中允许 ShareMe 使用屏幕录制权限，然后重新开始共享。"
            color: theme.textSecondary
            font.pixelSize: theme.fontMeta
            wrapMode: Text.WordWrap
        }

        Text {
            text: "键盘操作"
            color: theme.textPrimary
            font.pixelSize: theme.fontLabel
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: "Tab：移动焦点　　Enter / Space：执行按钮　　Esc：关闭对话框"
            color: theme.textSecondary
            font.pixelSize: theme.fontMeta
            wrapMode: Text.WordWrap
        }
    }
}
