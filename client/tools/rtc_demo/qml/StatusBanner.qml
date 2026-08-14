import QtQuick
import QtQuick.Layouts

Rectangle {
    id: banner
    property string text: ""
    property string kind: "info"
    visible: text.length > 0
    implicitHeight: visible ? theme.controlHeight : 0
    radius: theme.radiusMedium
    ShareMeTheme { id: theme }
    color: kind === "error" ? theme.errorSurface
         : kind === "warning" ? theme.warningSurface : theme.accentSubtle
    border.width: 1
    border.color: kind === "error" ? theme.error
                : kind === "warning" ? theme.warning : theme.accent

    Text {
        anchors.fill: parent
        anchors.margins: theme.spacingMd
        text: banner.text
        color: kind === "error" ? theme.error
             : kind === "warning" ? theme.warning : theme.accent
        font.pixelSize: theme.fontMeta
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
