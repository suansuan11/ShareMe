import QtQuick
import QtQuick.Layouts

Rectangle {
    id: banner
    property string text: ""
    property string kind: "info"
    visible: text.length > 0
    implicitHeight: visible ? 40 : 0
    radius: 9
    ShareMeTheme { id: theme }
    color: kind === "error" ? "#351B24"
         : kind === "warning" ? "#332B18" : "#10283D"
    border.width: 1
    border.color: kind === "error" ? theme.danger
                : kind === "warning" ? theme.warning : theme.primary

    Text {
        anchors.fill: parent
        anchors.margins: 11
        text: banner.text
        color: kind === "error" ? "#FFAFBA"
             : kind === "warning" ? "#FFE09A" : theme.cyan
        font.pixelSize: 12
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
