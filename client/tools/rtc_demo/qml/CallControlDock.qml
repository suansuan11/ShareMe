import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: dock
    required property var controller
    property bool detailsOpen: false
    signal toggleDetails()
    signal leaveRequested()
    implicitHeight: 60
    implicitWidth: controlsRow.implicitWidth + 20
    height: implicitHeight
    radius: theme.radiusLarge
    color: theme.surface
    border.width: 1
    border.color: theme.border
    ShareMeTheme { id: theme }

    RowLayout {
        id: controlsRow
        anchors.centerIn: parent
        spacing: theme.spacingSm
        IconControl {
            objectName: "microphoneControl"
            iconName: "microphone"
            active: !dock.controller.microphoneMuted
            muted: dock.controller.microphoneMuted
            accessibleDescription: dock.controller.microphoneMuted ? "开启麦克风" : "静音麦克风"
            onClicked: dock.controller.setMicrophoneMuted(!dock.controller.microphoneMuted)
        }
        IconControl {
            objectName: "speakerControl"
            iconName: "speaker"
            active: !dock.controller.speakerMuted
            muted: dock.controller.speakerMuted
            accessibleDescription: dock.controller.speakerMuted ? "开启扬声器" : "关闭扬声器"
            onClicked: dock.controller.setSpeakerMuted(!dock.controller.speakerMuted)
        }
        IconControl {
            objectName: "detailsControl"
            iconName: "details"
            active: dock.detailsOpen
            accessibleDescription: dock.detailsOpen ? "关闭通话详情" : "打开通话详情"
            onClicked: dock.toggleDetails()
        }
        IconControl {
            objectName: "leaveControl"
            iconName: "leave"
            destructive: true
            accessibleDescription: "离开通话"
            onClicked: dock.leaveRequested()
        }
    }
}
