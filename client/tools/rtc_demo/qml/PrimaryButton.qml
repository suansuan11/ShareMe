import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool secondary: false
    property string accessibleDescription: text
    implicitWidth: Math.max(132, contentItem.implicitWidth + 36)
    implicitHeight: theme.controlHeight
    hoverEnabled: true
    Accessible.name: accessibleDescription
    Accessible.description: accessibleDescription
    ToolTip.text: accessibleDescription
    ToolTip.visible: hovered && ToolTip.text.length > 0
    focusPolicy: Qt.StrongFocus
    font.pixelSize: theme.fontButton
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
        radius: theme.radiusMedium
        color: !control.enabled ? theme.surfaceDisabled
              : control.down ? (control.secondary ? theme.surfacePressed : theme.accentPressed)
              : control.hovered ? (control.secondary ? theme.surfaceHover : theme.accentHover)
              : control.secondary ? theme.surfaceRaised : theme.accent
        border.width: control.activeFocus ? 2 : control.secondary ? 1 : 0
        border.color: control.activeFocus ? theme.focus : theme.border
    }
}
