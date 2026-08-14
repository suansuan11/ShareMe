import QtQuick
import QtQuick.Layouts

RowLayout {
    id: row
    property string label: ""
    property string value: ""
    spacing: theme.spacingMd
    ShareMeTheme { id: theme }

    Text {
        Layout.fillWidth: true
        text: row.label
        color: theme.textSecondary
        font.pixelSize: theme.fontMeta
    }
    Text {
        text: row.value
        color: theme.textPrimary
        font.pixelSize: theme.fontMeta
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
}
