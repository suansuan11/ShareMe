import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    objectName: "callPage"
    required property QtObject appController
    required property var controller
    property bool detailsOpen: false
    property bool compact: width < 900
    ShareMeTheme { id: theme }

    Rectangle { anchors.fill: parent; color: theme.background }

    CallTopBar {
        id: topBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        controller: page.controller
        onRoomCopied: copyToast.restart()
    }

    Item {
        id: stageArea
        anchors.top: topBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        VideoStage {
            anchors.fill: parent
            controller: page.controller
        }

        CallDetailsDrawer {
            id: detailsDrawer
            visible: page.detailsOpen
            z: 2
            controller: page.controller
            compact: page.compact
            width: page.compact
                   ? Math.max(0, stageArea.width - 28)
                   : Math.min(theme.drawerWidth, Math.max(0, stageArea.width - 28))
            height: page.compact
                    ? Math.min(320, Math.max(180, stageArea.height * 0.52))
                    : Math.max(0, stageArea.height - 28)
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: 14
            anchors.bottomMargin: 14
        }

        CallControlDock {
            id: controlDock
            z: 3
            controller: page.controller
            width: Math.max(0, Math.min(stageArea.width - 32, implicitWidth))
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: page.compact && page.detailsOpen
                                ? detailsDrawer.height + 22 : 16
            detailsOpen: page.detailsOpen
            onToggleDetails: page.detailsOpen = !page.detailsOpen
            onLeaveRequested: page.appController.leaveCall()
        }
    }

    Rectangle {
        z: 4
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: page.compact && page.detailsOpen
                            ? detailsDrawer.height + controlDock.height + 38
                            : controlDock.height + 28
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
