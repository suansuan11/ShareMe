import QtQuick
import QtQuick.Layouts

RowLayout {
    id: row
    property string label: ""
    property string value: ""
    spacing: 12
    ShareMeTheme { id: theme }

    Text {
        Layout.fillWidth: true
        text: row.label
        color: theme.textSecondary
        font.pixelSize: 12
    }
    Text {
        text: row.value
        color: theme.textPrimary
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
}
