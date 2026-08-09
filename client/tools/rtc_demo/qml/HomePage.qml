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
        anchors.margins: 28
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Text {
                text: "ShareMe"
                color: theme.textPrimary
                font.pixelSize: 20
                font.weight: Font.Bold
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "设置"
                Accessible.name: "打开设置"
                flat: true
                onClicked: page.openSettings()
                contentItem: Text {
                    text: parent.text
                    color: parent.hovered ? theme.textPrimary : theme.textSecondary
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Item {}
            }
            Button {
                text: "帮助"
                Accessible.name: "打开帮助"
                flat: true
                onClicked: page.openHelp()
                contentItem: Text {
                    text: parent.text
                    color: parent.hovered ? theme.textPrimary : theme.textSecondary
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Item {}
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 20
            Layout.bottomMargin: 32
            spacing: 34

            ColumnLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 520
                spacing: 16
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: "一起看见，\n一起交流。"
                    color: theme.textPrimary
                    font.pixelSize: 46
                    font.weight: Font.Bold
                    lineHeight: 0.98
                }
                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 430
                    text: "高质量屏幕共享与清晰语音通话。无需账户，使用六位房间码即可连接。"
                    color: theme.textSecondary
                    font.pixelSize: 15
                    lineHeight: 1.35
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    spacing: 10
                    PrimaryButton {
                        text: "创建房间"
                        Accessible.name: "创建新的屏幕共享房间"
                        onClicked: page.appController.showCreateRoom()
                    }
                    PrimaryButton {
                        text: "加入房间"
                        secondary: true
                        Accessible.name: "使用房间码加入房间"
                        onClicked: page.appController.showJoinRoom()
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 420
                    Layout.preferredHeight: page.appController.recentRoom.length > 0 ? 58 : 0
                    visible: page.appController.recentRoom.length > 0
                    radius: 10
                    color: theme.surface
                    border.width: 1
                    border.color: theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 11
                        Text {
                            Layout.fillWidth: true
                            text: "最近房间  " + page.appController.formattedRecentRoom
                            color: theme.textSecondary
                            font.pixelSize: 12
                        }
                        Button {
                            text: "加入"
                            flat: true
                            palette.buttonText: theme.cyan
                            onClicked: page.appController.joinRecentRoom()
                        }
                        Button {
                            text: "忘记"
                            flat: true
                            palette.buttonText: theme.textMuted
                            onClicked: page.appController.forgetRecentRoom()
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 300
                Layout.maximumWidth: 510
                radius: 24
                color: theme.surface
                border.width: 1
                border.color: theme.border

                Rectangle {
                    width: Math.min(parent.width, parent.height) * 0.34
                    height: width
                    radius: 26
                    anchors.centerIn: parent
                    rotation: -6
                    gradient: Gradient {
                        GradientStop { position: 0; color: theme.cyan }
                        GradientStop { position: 1; color: "#6366F1" }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: "▣"
                        color: "white"
                        font.pixelSize: 56
                        font.weight: Font.Bold
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: "transparent"
                    border.width: 22
                    border.color: "#08111B"
                    opacity: 0.28
                }
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "端到端媒体传输 · 房间码连接 · 本地设置"
            color: theme.textMuted
            font.pixelSize: 11
        }
    }
}
