import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: dock
    required property var controller
    property bool detailsOpen: false
    signal toggleDetails()
    signal leaveRequested()
    implicitHeight: 76
    color: theme.surface
    ShareMeTheme { id: theme }

    RowLayout {
        anchors.centerIn: parent
        spacing: 10
        IconControl {
            symbol: dock.controller.microphoneMuted ? "╱" : "●"
            active: !dock.controller.microphoneMuted
            accessibleDescription: dock.controller.microphoneMuted ? "开启麦克风" : "静音麦克风"
            onClicked: dock.controller.setMicrophoneMuted(!dock.controller.microphoneMuted)
        }
        IconControl {
            symbol: dock.controller.speakerMuted ? "×" : "◖"
            active: !dock.controller.speakerMuted
            accessibleDescription: dock.controller.speakerMuted ? "开启扬声器" : "关闭扬声器"
            onClicked: dock.controller.setSpeakerMuted(!dock.controller.speakerMuted)
        }
        IconControl {
            symbol: "▣"
            active: !dock.controller.viewer
            enabled: false
            accessibleDescription: dock.controller.viewer ? "正在观看远端共享" : "屏幕共享已启用"
            ToolTip.text: accessibleDescription
        }
        IconControl {
            symbol: "⋯"
            active: dock.detailsOpen
            accessibleDescription: dock.detailsOpen ? "关闭通话详情" : "打开通话详情"
            onClicked: dock.toggleDetails()
        }
        IconControl {
            symbol: "⌁"
            destructive: true
            accessibleDescription: "离开通话"
            onClicked: dock.leaveRequested()
        }
    }
}
