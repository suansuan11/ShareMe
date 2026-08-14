import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string iconName: ""
    property string symbol: ""
    property bool destructive: false
    property bool active: false
    property bool muted: false
    property string accessibleDescription: text
    readonly property string resolvedIconName: iconName.length > 0 ? iconName
        : symbol === "╱" || symbol === "●" ? "microphone"
        : symbol === "×" || symbol === "◖" ? "speaker"
        : symbol === "⋯" ? "details"
        : symbol === "⌁" ? "leave"
        : symbol === "▣" ? "share" : ""
    readonly property bool resolvedMuted: iconName.length === 0
                                         && (symbol === "╱" || symbol === "×")
    implicitWidth: 44
    implicitHeight: 44
    hoverEnabled: true
    Accessible.name: accessibleDescription
    Accessible.description: accessibleDescription
    ToolTip.text: accessibleDescription
    ToolTip.visible: (hovered || activeFocus) && ToolTip.text.length > 0
    focusPolicy: Qt.StrongFocus

    ShareMeTheme { id: theme }
    contentItem: IconGlyph {
        name: control.resolvedIconName
        size: 20
        color: !control.enabled ? theme.textDisabled
               : control.destructive ? theme.error
               : control.active ? theme.accent : theme.textPrimary
        muted: control.muted || control.resolvedMuted
    }
    background: Rectangle {
        radius: theme.radiusMedium
        color: !control.enabled ? theme.surfaceDisabled
              : control.destructive ? (control.down || control.hovered
                                       ? theme.errorSurface : theme.surfaceRaised)
              : control.active ? theme.accentSubtle
              : control.down ? theme.surfacePressed
              : control.hovered ? theme.surfaceHover : theme.surfaceRaised
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? theme.focus
                      : control.destructive ? theme.error
                      : control.active ? theme.accent : theme.border
    }
}
