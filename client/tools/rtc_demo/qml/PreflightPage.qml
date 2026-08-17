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

    function interactionSurfaceColor(enabled, pressed, hovered, normalColor) {
        if (!enabled)
            return theme.surfaceDisabled
        if (pressed)
            return theme.surfacePressed
        if (hovered)
            return theme.surfaceHover
        return normalColor
    }

    function interactionBorderColor(enabled, focused) {
        if (focused)
            return theme.focus
        return enabled ? theme.border : theme.borderStrong
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
            Layout.fillHeight: false
            Layout.maximumWidth: 600
            Layout.maximumHeight: 500
            Layout.preferredHeight: page.compact
                                    ? Math.max(360, Math.min(416, page.height - 104))
                                    : page.createMode ? 420 : 460
            Layout.alignment: Qt.AlignHCenter
            color: theme.surface
            radius: theme.radiusLarge
            border.width: 1
            border.color: theme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: page.compact ? theme.spacingMd : 20
                spacing: page.compact ? theme.spacingSm : theme.spacingMd

                Text {
                    Layout.fillWidth: true
                    text: page.createMode ? "创建屏幕共享房间" : "加入屏幕共享房间"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontPageTitle - 2
                    font.weight: Font.DemiBold
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
                    text: page.createMode ? "共享内容" : "房间码"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontLabel
                    font.weight: Font.DemiBold
                }
                TextField {
                    id: roomCodeField
                    objectName: "roomCodeField"
                    Layout.fillWidth: true
                    visible: !page.createMode
                    enabled: !page.createMode
                    text: page.appController.roomCode
                    placeholderText: "例如 ABC-234"
                    maximumLength: 8
                    selectByMouse: true
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: true
                    property bool pointerPressed: false
                    Accessible.name: "六位房间码"
                    Accessible.description: "输入六位房间码，可使用连字符分隔"
                    onTextEdited: page.appController.roomCode = text
                    color: enabled ? theme.textPrimary : theme.textDisabled
                    placeholderTextColor: enabled ? theme.textMuted : theme.textDisabled
                    background: Rectangle {
                        implicitHeight: theme.controlHeight
                        radius: theme.radiusMedium
                        color: page.interactionSurfaceColor(
                                   roomCodeField.enabled,
                                   roomCodeField.pointerPressed,
                                   roomCodeField.hovered,
                                   theme.surfaceRaised)
                        border.width: roomCodeField.activeFocus ? 2 : 1
                        border.color: page.interactionBorderColor(
                                          roomCodeField.enabled,
                                          roomCodeField.activeFocus)
                    }
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onPressedChanged: roomCodeField.pointerPressed = pressed
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: page.createMode
                    text: "当前屏幕"
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
                    text: "声音"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontLabel
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.spacingSm

                    Switch {
                        id: microphoneIntentControl
                        objectName: "microphoneIntentControl"
                        Layout.fillWidth: true
                        text: "麦克风"
                        checked: page.appController.microphoneEnabled
                        Accessible.name: "通话意图：使用麦克风"
                        Accessible.description: checked ? "当前已开启" : "当前已关闭"
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        onToggled: page.appController.microphoneEnabled = checked
                        indicator: Rectangle {
                            implicitWidth: 52
                            implicitHeight: 28
                            x: microphoneIntentControl.text
                               ? (microphoneIntentControl.mirrored
                                  ? microphoneIntentControl.width - width
                                    - microphoneIntentControl.rightPadding
                                  : microphoneIntentControl.leftPadding)
                               : microphoneIntentControl.leftPadding
                                 + (microphoneIntentControl.availableWidth - width) / 2
                            y: microphoneIntentControl.topPadding
                               + (microphoneIntentControl.availableHeight - height) / 2
                            radius: height / 2
                            color: page.interactionSurfaceColor(
                                       microphoneIntentControl.enabled,
                                       microphoneIntentControl.down,
                                       microphoneIntentControl.hovered,
                                       microphoneIntentControl.checked
                                       ? theme.accentSubtle : theme.surfaceRaised)
                            border.width: microphoneIntentControl.visualFocus ? 2 : 1
                            border.color: page.interactionBorderColor(
                                              microphoneIntentControl.enabled,
                                              microphoneIntentControl.visualFocus)
                            Rectangle {
                                width: 20
                                height: 20
                                x: (parent.width - width)
                                   * microphoneIntentControl.visualPosition
                                y: (parent.height - height) / 2
                                radius: width / 2
                                color: !microphoneIntentControl.enabled
                                       ? theme.textDisabled
                                       : microphoneIntentControl.down
                                       ? theme.accentPressed
                                       : microphoneIntentControl.checked
                                       ? theme.accent : theme.textSecondary
                                border.width: 1
                                border.color: microphoneIntentControl.enabled
                                              ? theme.borderStrong : theme.textDisabled
                            }
                        }
                        contentItem: Text {
                            leftPadding: microphoneIntentControl.indicator.width
                                         + microphoneIntentControl.spacing
                            rightPadding: 0
                            text: microphoneIntentControl.text
                            color: microphoneIntentControl.enabled
                                   ? theme.textPrimary : theme.textDisabled
                            font: microphoneIntentControl.font
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Switch {
                        id: speakerIntentControl
                        objectName: "speakerIntentControl"
                        Layout.fillWidth: true
                        text: "播放对方声音"
                        checked: page.appController.speakerEnabled
                        Accessible.name: "通话意图：播放对方声音"
                        Accessible.description: checked ? "当前已开启" : "当前已关闭"
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        onToggled: page.appController.speakerEnabled = checked
                        indicator: Rectangle {
                            implicitWidth: 52
                            implicitHeight: 28
                            x: speakerIntentControl.text
                               ? (speakerIntentControl.mirrored
                                  ? speakerIntentControl.width - width
                                    - speakerIntentControl.rightPadding
                                  : speakerIntentControl.leftPadding)
                               : speakerIntentControl.leftPadding
                                 + (speakerIntentControl.availableWidth - width) / 2
                            y: speakerIntentControl.topPadding
                               + (speakerIntentControl.availableHeight - height) / 2
                            radius: height / 2
                            color: page.interactionSurfaceColor(
                                       speakerIntentControl.enabled,
                                       speakerIntentControl.down,
                                       speakerIntentControl.hovered,
                                       speakerIntentControl.checked
                                       ? theme.accentSubtle : theme.surfaceRaised)
                            border.width: speakerIntentControl.visualFocus ? 2 : 1
                            border.color: page.interactionBorderColor(
                                              speakerIntentControl.enabled,
                                              speakerIntentControl.visualFocus)
                            Rectangle {
                                width: 20
                                height: 20
                                x: (parent.width - width)
                                   * speakerIntentControl.visualPosition
                                y: (parent.height - height) / 2
                                radius: width / 2
                                color: !speakerIntentControl.enabled
                                       ? theme.textDisabled
                                       : speakerIntentControl.down
                                       ? theme.accentPressed
                                       : speakerIntentControl.checked
                                       ? theme.accent : theme.textSecondary
                                border.width: 1
                                border.color: speakerIntentControl.enabled
                                              ? theme.borderStrong : theme.textDisabled
                            }
                        }
                        contentItem: Text {
                            leftPadding: speakerIntentControl.indicator.width
                                         + speakerIntentControl.spacing
                            rightPadding: 0
                            text: speakerIntentControl.text
                            color: speakerIntentControl.enabled
                                   ? theme.textPrimary : theme.textDisabled
                            font: speakerIntentControl.font
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: "共享质量"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontLabel
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
                    hoverEnabled: true
                    Accessible.name: "共享质量"
                    Accessible.description: enabled ? "选择共享画质" : "加入房间时使用创建者的共享质量"
                    onActivated: page.appController.screenProfile =
                                 currentIndex === 1 ? "quality"
                                 : currentIndex === 2 ? "cinema" : "standard"
                    indicator: IconGlyph {
                        x: qualityProfileControl.width - width - theme.spacingMd
                        y: (qualityProfileControl.height - height) / 2
                        size: 16
                        name: "chevron"
                        strokeWidth: theme.iconStrokeWidth
                        color: qualityProfileControl.enabled
                               ? theme.textSecondary : theme.textDisabled
                    }
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
                        color: page.interactionSurfaceColor(
                                   qualityProfileControl.enabled,
                                   qualityProfileControl.down,
                                   qualityProfileControl.hovered,
                                   theme.surfaceRaised)
                        border.width: qualityProfileControl.activeFocus ? 2 : 1
                        border.color: page.interactionBorderColor(
                                          qualityProfileControl.enabled,
                                          qualityProfileControl.activeFocus)
                    }
                }

                PrimaryButton {
                    id: preflightPrimaryButton
                    objectName: "preflightPrimaryButton"
                    Layout.fillWidth: true
                    text: page.createMode ? "创建并开始共享" : "加入通话"
                    enabled: page.createMode || page.roomCodeValid
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    accessibleDescription: enabled ? text : "请输入有效的六位房间码"
                    Accessible.name: enabled ? text : "请输入有效的六位房间码"
                    onClicked: page.appController.startCall()
                }
            }
        }
    }
}
