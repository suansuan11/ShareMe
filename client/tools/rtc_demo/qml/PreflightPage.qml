import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property QtObject appController
    ShareMeTheme { id: theme }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "‹  返回"
                flat: true
                Accessible.name: "返回首页"
                palette.buttonText: theme.textSecondary
                onClicked: page.appController.returnHome()
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "ShareMe"
                color: theme.textPrimary
                font.pixelSize: 18
                font.weight: Font.Bold
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 20

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 330
                radius: 16
                color: "#05080D"
                border.width: 1
                border.color: theme.border
                Column {
                    anchors.centerIn: parent
                    spacing: 12
                    Rectangle {
                        width: 76; height: 76; radius: 38
                        anchors.horizontalCenter: parent.horizontalCenter
                        gradient: Gradient {
                            GradientStop { position: 0; color: theme.cyan }
                            GradientStop { position: 1; color: "#6366F1" }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: "D"
                            color: "white"
                            font.pixelSize: 28
                            font.weight: Font.Bold
                        }
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: page.appController.preflight === "create"
                              ? "准备共享当前屏幕" : "加入后将在这里显示共享画面"
                        color: theme.textSecondary
                        font.pixelSize: 13
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 380
                Layout.fillHeight: true
                radius: 16
                color: theme.surface
                border.width: 1
                border.color: theme.border

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 22
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 13
                        Text {
                            text: page.appController.preflight === "create"
                                  ? "创建屏幕共享房间" : "加入屏幕共享房间"
                            color: theme.textPrimary
                            font.pixelSize: 22
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: page.appController.preflight === "create"
                                  ? "房间创建后，将房间码发给另一位参与者。"
                                  : "输入对方提供的六位房间码。"
                            color: theme.textSecondary
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            visible: page.appController.preflight === "join"
                            text: "房间码"
                            color: theme.textSecondary
                            font.pixelSize: 12
                        }
                        TextField {
                            Layout.fillWidth: true
                            visible: page.appController.preflight === "join"
                            text: page.appController.roomCode
                            placeholderText: "例如 ABC-234"
                            maximumLength: 8
                            Accessible.name: "六位房间码"
                            onTextEdited: page.appController.roomCode = text
                            color: theme.textPrimary
                            placeholderTextColor: theme.textMuted
                            background: Rectangle {
                                implicitHeight: 44
                                radius: 8
                                color: theme.surfaceRaised
                                border.width: 1
                                border.color: parent.activeFocus ? theme.primary : theme.border
                            }
                        }
                        Text { text: "共享质量"; color: theme.textSecondary; font.pixelSize: 12 }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["标准 · 最高 1080p60", "高质量 · 最高 1440p60", "影院 · 最高 4K30"]
                            currentIndex: page.appController.screenProfile === "quality" ? 1
                                        : page.appController.screenProfile === "cinema" ? 2 : 0
                            enabled: page.appController.preflight === "create"
                            Accessible.name: "屏幕共享质量"
                            onActivated: page.appController.screenProfile =
                                         currentIndex === 1 ? "quality"
                                         : currentIndex === 2 ? "cinema" : "standard"
                        }
                        Switch {
                            text: "开启麦克风"
                            checked: page.appController.microphoneEnabled
                            Accessible.name: "加入时开启麦克风"
                            onToggled: page.appController.microphoneEnabled = checked
                        }
                        Switch {
                            text: "播放对方声音"
                            checked: page.appController.speakerEnabled
                            Accessible.name: "加入时播放对方声音"
                            onToggled: page.appController.speakerEnabled = checked
                        }
                        StatusBanner {
                            Layout.fillWidth: true
                            text: page.appController.errorMessage
                            kind: "error"
                        }
                        Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }
                        PrimaryButton {
                            Layout.fillWidth: true
                            text: page.appController.preflight === "create" ? "创建并开始共享" : "加入通话"
                            Accessible.name: text
                            onClicked: page.appController.startCall()
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "麦克风或屏幕权限缺失时，ShareMe 会给出修复提示，不会静默降级。"
                            color: theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
