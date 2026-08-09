import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string symbol: ""
    property bool destructive: false
    property bool active: false
    property string accessibleDescription: text
    implicitWidth: 46
    implicitHeight: 42
    hoverEnabled: true
    Accessible.name: accessibleDescription
    ToolTip.text: accessibleDescription
    ToolTip.visible: hovered

    ShareMeTheme { id: theme }
    contentItem: Text {
        text: control.symbol
        color: theme.textPrimary
        font.pixelSize: 18
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 12
        color: control.destructive ? theme.danger
              : control.active ? theme.primary
              : control.hovered ? theme.surfaceHover : theme.surfaceRaised
        border.width: control.active ? 0 : 1
        border.color: theme.border
    }
}
