import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property QtObject appController
    required property var controller
    property bool detailsOpen: false
    property bool compact: width < 900
    ShareMeTheme { id: theme }

    Rectangle { anchors.fill: parent; color: theme.background }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        CallTopBar {
            Layout.fillWidth: true
            controller: page.controller
            onRoomCopied: copyToast.restart()
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            VideoStage {
                anchors.fill: parent
                anchors.margins: 14
                anchors.rightMargin: !page.compact && page.detailsOpen ? 342 : 14
                controller: page.controller
            }
            CallDetailsDrawer {
                visible: page.detailsOpen
                controller: page.controller
                compact: page.compact
                width: page.compact ? parent.width - 28 : 314
                height: page.compact ? Math.min(330, parent.height - 28) : parent.height - 28
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 14
            }
        }
        CallControlDock {
            Layout.fillWidth: true
            controller: page.controller
            detailsOpen: page.detailsOpen
            onToggleDetails: page.detailsOpen = !page.detailsOpen
            onLeaveRequested: page.appController.leaveCall()
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 88
        width: copyText.implicitWidth + 24
        height: 34
        radius: 9
        color: theme.surfaceRaised
        opacity: copyToast.running ? 1 : 0
        visible: opacity > 0
        Text { id: copyText; anchors.centerIn: parent; text: "房间码已复制"; color: theme.textPrimary; font.pixelSize: 11 }
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }
    Timer { id: copyToast; interval: 1600 }
}
