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
                width: Math.min(parent.width, 620)
                spacing: theme.spacingLg

                Text {
                    Layout.fillWidth: true
                    text: "一起共享，\n一起交流。"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontDisplay + 8
                    font.weight: Font.Bold
                    lineHeight: 0.98
                }
                Text {
                    Layout.fillWidth: true
                    text: "屏幕共享与语音通话，使用房间码即可开始。"
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
                    implicitHeight: visible ? 64 : 0
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
