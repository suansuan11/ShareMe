import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogSurface {
    id: dialog
    objectName: "settingsDialog"
    required property QtObject appController
    title: "设置"
    closeAccessibleDescription: "关闭设置"
    ShareMeTheme { id: theme }

    contentItem: ColumnLayout {
        spacing: theme.spacingSm
        Text {
            text: "连接地址"
            color: theme.textPrimary
            font.pixelSize: theme.fontLabel
            font.weight: Font.DemiBold
        }
        TextField {
            id: serverUrlField
            objectName: "serverUrlField"
            Layout.fillWidth: true
            implicitHeight: theme.controlHeight
            text: dialog.appController.serverUrl
            Accessible.name: "连接地址"
            placeholderText: "ws://127.0.0.1:8080/v1/ws"
            onEditingFinished: dialog.appController.serverUrl = text
        }
        Text {
            Layout.fillWidth: true
            text: "这是开发环境的连接设置，不会和房间数据一起保存。"
            color: theme.textMuted
            font.pixelSize: theme.fontCaption
            wrapMode: Text.WordWrap
        }

        Connections {
            target: dialog.appController
            function onServerUrlChanged() {
                if (!serverUrlField.activeFocus)
                    serverUrlField.text = dialog.appController.serverUrl
            }
        }
    }
}
