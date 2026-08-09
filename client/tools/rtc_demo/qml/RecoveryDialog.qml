import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: recovery
    required property QtObject appController
    color: theme.background
    ShareMeTheme { id: theme }
    Rectangle {
        width: Math.min(520, parent.width - 48)
        implicitHeight: content.implicitHeight + 48
        anchors.centerIn: parent
        radius: 16
        color: theme.surface
        border.width: 1
        border.color: theme.border
        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 14
            Rectangle {
                width: 44; height: 44; radius: 13
                color: "#35251C"
                Text { anchors.centerIn: parent; text: "!"; color: theme.warning; font.pixelSize: 22; font.weight: Font.Bold }
            }
            Text { text: "通话暂时无法继续"; color: theme.textPrimary; font.pixelSize: 20; font.weight: Font.Bold }
            Text {
                Layout.fillWidth: true
                text: recovery.appController.errorMessage.length > 0
                      ? recovery.appController.errorMessage
                      : "通话遇到问题，请重试或返回首页。"
                color: theme.textSecondary
                font.pixelSize: 13
                lineHeight: 1.4
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                PrimaryButton { text: "返回首页"; secondary: true; onClicked: recovery.appController.returnHome() }
                PrimaryButton { text: "重试"; onClicked: recovery.appController.retryCall() }
            }
            Text {
                Layout.fillWidth: true
                text: recovery.appController.errorCategory.length > 0
                      ? "诊断类别：" + recovery.appController.errorCategory : ""
                color: theme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WrapAnywhere
            }
        }
    }
}
