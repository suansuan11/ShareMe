import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property QtObject appController
    signal openSettings()
    signal openHelp()

    ShareMeTheme { id: theme }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.spacingXl
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.controlHeight
            spacing: theme.spacingXs

            Text {
                text: "ShareMe"
                color: theme.textPrimary
                font.pixelSize: theme.fontSectionTitle
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            IconControl {
                objectName: "settingsAction"
                iconName: "settings"
                accessibleDescription: "打开设置"
                onClicked: page.openSettings()
            }
            IconControl {
                objectName: "helpAction"
                iconName: "help"
                accessibleDescription: "打开帮助"
                onClicked: page.openHelp()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width, 640)
                spacing: theme.spacingMd

                Text {
                    Layout.fillWidth: true
                    text: "共享屏幕，保持交流。"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontDisplay + 2
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    text: "创建房间，或输入房间码加入。"
                    color: theme.textSecondary
                    font.pixelSize: theme.fontBody
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: theme.spacingSm

                    PrimaryButton {
                        objectName: "createRoomButton"
                        text: "创建房间"
                        accessibleDescription: "创建新的屏幕共享房间"
                        onClicked: page.appController.showCreateRoom()
                    }
                    PrimaryButton {
                        objectName: "joinRoomButton"
                        text: "加入房间"
                        secondary: true
                        accessibleDescription: "使用房间码加入房间"
                        onClicked: page.appController.showJoinRoom()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: visible ? 56 : 0
                    visible: page.appController.recentRoom.length > 0
                    color: theme.surface
                    radius: theme.radiusMedium
                    border.width: 1
                    border.color: theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: theme.spacingMd
                        spacing: theme.spacingSm

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: theme.spacingXs
                            Text {
                                text: "最近房间"
                                color: theme.textMuted
                                font.pixelSize: theme.fontCaption
                            }
                            Text {
                                text: page.appController.formattedRecentRoom
                                color: theme.textPrimary
                                font.pixelSize: theme.fontBody
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                        }
                        PrimaryButton {
                            objectName: "recentRoomAction"
                            text: "重新加入"
                            secondary: true
                            accessibleDescription: "重新加入最近的房间"
                            onClicked: page.appController.joinRecentRoom()
                        }
                        PrimaryButton {
                            text: "忘记"
                            secondary: true
                            accessibleDescription: "忘记最近的房间"
                            onClicked: page.appController.forgetRecentRoom()
                        }
                    }
                }
            }
        }
    }
}
