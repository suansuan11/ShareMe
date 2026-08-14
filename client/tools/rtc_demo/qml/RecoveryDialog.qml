import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: recovery
    objectName: "recoverySurface"
    required property QtObject appController
    property bool smokePreview: false
    color: theme.background
    ShareMeTheme { id: theme }

    function recoveryTitle(category) {
        var normalized = String(category).toLowerCase()
        if (normalized === "permission-denied")
            return "需要检查权限"
        if (normalized === "invalid-room")
            return "无法加入这个房间"
        if (normalized.indexOf("screen") >= 0 ||
                normalized.indexOf("capture") >= 0)
            return "屏幕共享不可用"
        if (normalized.indexOf("audio") >= 0 ||
                normalized.indexOf("device") >= 0)
            return "声音设备不可用"
        if (normalized.indexOf("connection") >= 0 ||
                normalized.indexOf("ice") >= 0 ||
                normalized.indexOf("timed out") >= 0)
            return "连接未建立"
        return "通话暂时无法继续"
    }

    Rectangle {
        width: Math.min(520, parent.width - theme.spacingXl * 2)
        implicitHeight: content.implicitHeight + theme.spacingXl * 2
        anchors.centerIn: parent
        radius: theme.radiusLarge
        color: theme.surface
        border.width: 1
        border.color: theme.border
        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: theme.spacingXl
            spacing: theme.spacingMd
            Rectangle {
                width: theme.controlHeight
                height: theme.controlHeight
                radius: theme.radiusLarge
                color: theme.warningSurface
                Text { anchors.centerIn: parent; text: "!"; color: theme.warning; font.pixelSize: theme.fontPageTitle; font.weight: Font.Bold }
            }
            Text {
                Layout.fillWidth: true
                text: recovery.recoveryTitle(recovery.appController.errorCategory)
                color: theme.textPrimary
                font.pixelSize: theme.fontPageTitle
                font.weight: Font.Bold
            }
            Text {
                Layout.fillWidth: true
                text: recovery.appController.errorMessage.length > 0
                      ? recovery.appController.errorMessage
                      : "通话遇到问题，请重试或返回首页。"
                color: theme.textSecondary
                font.pixelSize: theme.fontLabel
                lineHeight: theme.lineHeightBody
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: theme.spacingSm
                PrimaryButton { text: "返回首页"; secondary: true; onClicked: recovery.appController.returnHome() }
                PrimaryButton { text: "重试"; onClicked: recovery.appController.retryCall() }
            }
        }
    }
}
