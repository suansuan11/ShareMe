import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    property string closeAccessibleDescription: "关闭对话框"
    property int contentMargins: theme.spacingXl
    modal: true
    anchors.centerIn: parent
    width: Math.min(480, parent ? parent.width - theme.spacingXxl : 480)
    padding: contentMargins
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape

    ShareMeTheme { id: theme }

    header: Item {
        implicitHeight: theme.controlHeight
        RowLayout {
            anchors.fill: parent
            spacing: theme.spacingSm
            Text {
                Layout.fillWidth: true
                text: dialog.title
                color: theme.textPrimary
                font.pixelSize: theme.fontSectionTitle
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Accessible.name: dialog.title
            }
            IconControl {
                id: closeButton
                objectName: "dialogSurfaceClose"
                iconName: "close"
                implicitWidth: theme.controlHeight
                implicitHeight: theme.controlHeight
                accessibleDescription: dialog.closeAccessibleDescription
                onClicked: dialog.reject()
            }
        }
    }

    background: Rectangle {
        color: theme.surface
        radius: theme.radiusLarge
        border.width: closeButton.activeFocus ? 2 : 1
        border.color: closeButton.activeFocus ? theme.focus : theme.border
    }

    onOpened: closeButton.forceActiveFocus()
}
