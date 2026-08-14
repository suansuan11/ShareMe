import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property QtObject appController
    readonly property bool createMode: appController.preflight === "create"
    readonly property bool compact: width < 900 || height < 600
    readonly property bool roomCodeValid: /^[A-Z2-7]{6}$/.test(
        normalizedRoomCode(appController.roomCode))

    function normalizedRoomCode(value) {
        return value.toUpperCase().replace(/[\s-]/g, "")
    }

    ShareMeTheme { id: theme }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: page.compact ? theme.spacingLg : theme.spacingXl
        spacing: page.compact ? theme.spacingSm : theme.spacingMd

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.controlHeight
            spacing: theme.spacingSm

            IconControl {
                objectName: "preflightBackButton"
                iconName: "back"
                accessibleDescription: "返回首页"
                Accessible.name: "返回首页"
                onClicked: page.appController.returnHome()
            }
            Text {
                Layout.fillWidth: true
                text: page.createMode ? "创建房间" : "加入房间"
                color: theme.textPrimary
                font.pixelSize: theme.fontSectionTitle
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                text: "ShareMe"
                color: theme.textMuted
                font.pixelSize: theme.fontMeta
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            id: surface
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 680
            Layout.alignment: Qt.AlignHCenter
            color: theme.surface
            radius: theme.radiusLarge
            border.width: 1
            border.color: theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: page.compact ? theme.spacingMd : theme.spacingXl
                spacing: page.compact ? theme.spacingSm : theme.spacingMd

                Text {
                    Layout.fillWidth: true
                    text: page.createMode ? "创建屏幕共享房间" : "加入屏幕共享房间"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontPageTitle
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: page.createMode
                          ? "通话开始后，会共享当前屏幕给加入房间的参与者。"
                          : "输入对方提供的六位房间码即可加入。"
                    color: theme.textSecondary
                    font.pixelSize: theme.fontMeta
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: "房间"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSectionTitle
                    font.weight: Font.DemiBold
                }
                TextField {
                    id: roomCodeField
                    objectName: "roomCodeField"
                    Layout.fillWidth: true
                    visible: !page.createMode
                    text: page.appController.roomCode
                    placeholderText: "例如 ABC-234"
                    maximumLength: 8
                    selectByMouse: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: "六位房间码"
                    onTextEdited: page.appController.roomCode = text
                    color: theme.textPrimary
                    placeholderTextColor: theme.textMuted
                    background: Rectangle {
                        implicitHeight: theme.controlHeight
                        radius: theme.radiusMedium
                        color: theme.surfaceRaised
                        border.width: roomCodeField.activeFocus ? 2 : 1
                        border.color: roomCodeField.activeFocus
                                      ? theme.focus : theme.border
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.createMode
                    text: "当前屏幕会作为共享画面发送，不需要选择设备。"
                    color: theme.textSecondary
                    font.pixelSize: theme.fontMeta
                    wrapMode: Text.WordWrap
                }
                StatusBanner {
                    Layout.fillWidth: true
                    text: page.appController.errorMessage
                    kind: "error"
                }

                Text {
                    Layout.fillWidth: true
                    text: "设备"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSectionTitle
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingSm

                    Switch {
                        id: microphoneIntentControl
                        objectName: "microphoneIntentControl"
                        Layout.fillWidth: true
                        text: "麦克风（当前意图）"
                        checked: page.appController.microphoneEnabled
                        Accessible.name: "通话意图：使用麦克风"
                        focusPolicy: Qt.StrongFocus
                        onToggled: page.appController.microphoneEnabled = checked
                    }
                    Switch {
                        id: speakerIntentControl
                        objectName: "speakerIntentControl"
                        Layout.fillWidth: true
                        text: "扬声器（当前意图）"
                        checked: page.appController.speakerEnabled
                        Accessible.name: "通话意图：播放对方声音"
                        focusPolicy: Qt.StrongFocus
                        onToggled: page.appController.speakerEnabled = checked
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "共享质量"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontSectionTitle
                    font.weight: Font.DemiBold
                }
                ComboBox {
                    id: qualityProfileControl
                    objectName: "qualityProfileControl"
                    Layout.fillWidth: true
                    model: ["1080p 60 · 流畅", "1440p 60 · 高画质", "4K 30 · 影院"]
                    currentIndex: page.appController.screenProfile === "quality" ? 1
                                : page.appController.screenProfile === "cinema" ? 2 : 0
                    enabled: page.createMode
                    focusPolicy: Qt.StrongFocus
                    Accessible.name: "共享质量"
                    onActivated: page.appController.screenProfile =
                                 currentIndex === 1 ? "quality"
                                 : currentIndex === 2 ? "cinema" : "standard"
                    contentItem: Text {
                        leftPadding: theme.spacingMd
                        rightPadding: theme.spacingXl
                        text: qualityProfileControl.displayText
                        color: qualityProfileControl.enabled
                               ? theme.textPrimary : theme.textDisabled
                        font.pixelSize: theme.fontBody
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        implicitHeight: theme.controlHeight
                        radius: theme.radiusMedium
                        color: qualityProfileControl.enabled
                               ? theme.surfaceRaised : theme.surfaceDisabled
                        border.width: qualityProfileControl.activeFocus ? 2 : 1
                        border.color: qualityProfileControl.activeFocus
                                      ? theme.focus : theme.border
                    }
                }

                PrimaryButton {
                    id: preflightPrimaryButton
                    objectName: "preflightPrimaryButton"
                    Layout.fillWidth: true
                    text: page.createMode ? "创建并开始共享" : "加入通话"
                    enabled: page.createMode || page.roomCodeValid
                    accessibleDescription: enabled ? text : "请输入有效的六位房间码"
                    Accessible.name: enabled ? text : "请输入有效的六位房间码"
                    onClicked: page.appController.startCall()
                }
            }
        }
    }
}
