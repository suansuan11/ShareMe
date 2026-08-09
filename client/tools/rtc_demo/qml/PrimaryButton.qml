import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool secondary: false
    property string accessibleDescription: text
    implicitWidth: Math.max(132, contentItem.implicitWidth + 36)
    implicitHeight: 44
    hoverEnabled: true
    Accessible.name: accessibleDescription
    ToolTip.visible: hovered && ToolTip.text.length > 0
    font.pixelSize: 14
    font.weight: Font.DemiBold

    ShareMeTheme { id: theme }

    contentItem: Text {
        text: control.text
        color: control.enabled ? theme.textPrimary : theme.textMuted
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 9
        color: !control.enabled ? theme.surfaceRaised
              : control.down ? (control.secondary ? theme.border : "#0F70B7")
              : control.hovered ? (control.secondary ? theme.surfaceHover : theme.primaryHover)
              : control.secondary ? theme.surfaceRaised : theme.primary
        border.width: control.secondary ? 1 : 0
        border.color: theme.border
    }
}
