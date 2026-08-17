import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: drawer
    required property var controller
    property bool compact: false
    color: theme.surface
    border.width: 1
    border.color: theme.border
    radius: theme.radiusLarge
    ShareMeTheme { id: theme }

    function statusLabel(status) {
        if (status === "connected")
            return "连接稳定"
        if (status === "signaling")
            return "正在连接房间"
        if (status === "negotiating" || status === "connecting")
            return "正在连接"
        if (status.startsWith("screen-capture-recovering:"))
            return "正在恢复屏幕共享"
        if (status.startsWith("session-suspended:"))
            return "系统暂停，等待恢复"
        if (status === "session-resuming")
            return "正在恢复通话"
        if (status === "waiting-for-viewer")
            return "等待参与者"
        if (status === "ended")
            return "通话已结束"
        return "连接中"
    }

    function profileLabel(profile) {
        if (profile === "standard")
            return "1080p 60 · 流畅"
        if (profile === "quality")
            return "1440p 60 · 高画质"
        if (profile === "cinema")
            return "4K 30 · 影院"
        return "不可用"
    }

    function sourceLabel(source) {
        if (source === "screen")
            return "屏幕共享"
        if (source === "desktop")
            return "桌面共享"
        if (source === "movie")
            return "影片"
        if (source === "test")
            return "测试画面"
        return "不可用"
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: theme.spacingLg
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: theme.spacingMd
            Text {
                text: "通话详情"
                color: theme.textPrimary
                font.pixelSize: theme.fontSectionTitle
                font.weight: Font.Bold
            }

            ColumnLayout {
                id: connectionSection
                objectName: "connectionSection"
                Layout.fillWidth: true
                spacing: theme.spacingSm
                Text {
                    text: "通话概况"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontLabel
                    font.weight: Font.DemiBold
                }
                InfoRow {
                    Layout.fillWidth: true
                    label: "角色"
                    value: drawer.controller.viewer ? "参与者" : "主持人"
                }
                InfoRow {
                    Layout.fillWidth: true
                    label: "状态"
                    value: drawer.statusLabel(drawer.controller.status)
                }
                InfoRow {
                    Layout.fillWidth: true
                    label: "共享"
                    value: drawer.sourceLabel(drawer.controller.videoSource)
                }
                InfoRow {
                    Layout.fillWidth: true
                    label: "质量"
                    value: drawer.profileLabel(drawer.controller.screenProfile)
                }
            }

            ColumnLayout {
                id: videoSection
                objectName: "videoSection"
                visible: false
                Layout.fillWidth: true
                spacing: theme.spacingSm
                Text {
                    text: "画面"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontLabel
                    font.weight: Font.DemiBold
                }
                InfoRow {
                    Layout.fillWidth: true
                    label: "共享质量"
                    value: drawer.profileLabel(drawer.controller.screenProfile)
                }
                InfoRow {
                    Layout.fillWidth: true
                    label: "视频来源"
                    value: drawer.sourceLabel(drawer.controller.videoSource)
                }
            }

            ColumnLayout {
                id: audioSection
                objectName: "audioSection"
                Layout.fillWidth: true
                spacing: theme.spacingSm
                Text {
                    text: "声音"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontLabel
                    font.weight: Font.DemiBold
                }
                InfoRow {
                    Layout.fillWidth: true
                    label: "麦克风"
                    value: drawer.controller.microphoneMuted ? "已静音" : "已开启"
                }
                InfoRow {
                    Layout.fillWidth: true
                    label: "扬声器"
                    value: drawer.controller.speakerMuted ? "已关闭" : "已开启"
                }

                ColumnLayout {
                    id: voicePanel
                    objectName: "voicePanel"
                    Layout.fillWidth: true
                    spacing: theme.spacingSm
                    Text {
                        Layout.fillWidth: true
                        text: drawer.controller.voiceQualityMessage
                        color: drawer.controller.voiceQuality === "poor" ? theme.error
                             : drawer.controller.voiceQuality === "unstable" ? theme.warning
                             : drawer.controller.voiceQuality === "good" ? theme.cyan
                             : theme.textSecondary
                        font.pixelSize: theme.fontCaption
                    }
                    Text { text: "麦克风活动"; color: theme.textSecondary; font.pixelSize: theme.fontCaption }
                    ProgressBar {
                        id: voiceMeter
                        objectName: "voiceMeter"
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: drawer.controller.microphoneLevel
                        Accessible.name: "麦克风活动"
                        background: Rectangle {
                            implicitHeight: 4
                            radius: height / 2
                            color: theme.surfaceRaised
                        }
                        contentItem: Item {
                            implicitHeight: 4
                            Rectangle {
                                width: parent.width * voiceMeter.visualPosition
                                height: parent.height
                                radius: height / 2
                                color: theme.accent
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "对方音量"; color: theme.textSecondary; font.pixelSize: theme.fontCaption }
                        Slider {
                            id: speakerVolumeControl
                            objectName: "speakerVolumeControl"
                            function requestVolume(requested) {
                                value = requested
                                if (!drawer.controller.setSpeakerVolume(Math.round(requested)))
                                    value = drawer.controller.speakerVolume
                                return value === drawer.controller.speakerVolume
                            }
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            stepSize: 1
                            value: drawer.controller.speakerVolume
                            enabled: drawer.controller.speakerVolumeAvailable
                            Accessible.name: "对方声音音量"
                            onMoved: requestVolume(value)
                            background: Rectangle {
                                x: speakerVolumeControl.leftPadding
                                y: speakerVolumeControl.topPadding
                                   + speakerVolumeControl.availableHeight / 2 - height / 2
                                implicitWidth: 160
                                implicitHeight: 4
                                width: speakerVolumeControl.availableWidth
                                height: implicitHeight
                                radius: height / 2
                                color: theme.surfaceRaised
                                Rectangle {
                                    width: parent.width * speakerVolumeControl.visualPosition
                                    height: parent.height
                                    radius: height / 2
                                    color: speakerVolumeControl.enabled
                                           ? theme.accent : theme.textDisabled
                                }
                            }
                            handle: Rectangle {
                                x: speakerVolumeControl.leftPadding
                                   + speakerVolumeControl.visualPosition
                                   * (speakerVolumeControl.availableWidth - width)
                                y: speakerVolumeControl.topPadding
                                   + speakerVolumeControl.availableHeight / 2 - height / 2
                                implicitWidth: 16
                                implicitHeight: 16
                                radius: width / 2
                                color: speakerVolumeControl.enabled
                                       ? theme.textPrimary : theme.textDisabled
                                border.width: speakerVolumeControl.activeFocus ? 2 : 1
                                border.color: speakerVolumeControl.activeFocus
                                              ? theme.focus : theme.borderStrong
                            }
                        }
                        Text { text: drawer.controller.speakerVolume + "%"; color: theme.textSecondary; font.pixelSize: theme.fontCaption }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: drawer.controller.voiceProcessingSummary
                        color: theme.textMuted
                        font.pixelSize: theme.fontCaption
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }

            ColumnLayout {
                id: playbackSection
                objectName: "playbackSection"
                Layout.fillWidth: true
                visible: !drawer.controller.viewer && drawer.controller.hostControlsAvailable
                spacing: theme.spacingSm
                Text { text: "播放"; color: theme.textPrimary; font.pixelSize: theme.fontLabel; font.weight: Font.DemiBold }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: drawer.controller.hostPlaybackState === "playing" ? "暂停" : "继续"
                        onClicked: drawer.controller.hostPlaybackState === "playing"
                                   ? drawer.controller.pauseHostPlayback()
                                   : drawer.controller.resumeHostPlayback()
                    }
                    Text {
                        Layout.fillWidth: true
                        text: Math.max(0, drawer.controller.hostPlaybackPositionMs
                                      - drawer.controller.hostPlaybackStartMs) + " / "
                              + drawer.controller.hostPlaybackDurationMs + " ms"
                        color: theme.textSecondary
                        font.pixelSize: theme.fontCaption
                        elide: Text.ElideRight
                    }
                }
                Slider {
                    Layout.fillWidth: true
                    Layout.minimumHeight: theme.controlHeight
                    implicitHeight: theme.controlHeight
                    Accessible.name: "播放进度"
                    from: 0
                    to: Math.max(0, drawer.controller.hostPlaybackDurationMs)
                    value: Math.max(0, drawer.controller.hostPlaybackPositionMs
                                       - drawer.controller.hostPlaybackStartMs)
                    onMoved: drawer.controller.seekHostPlayback(
                                 drawer.controller.hostPlaybackStartMs + Math.round(value))
                }
            }

            Button {
                id: advancedButton
                objectName: "advancedControl"
                Layout.fillWidth: true
                property bool expanded: false
                text: expanded ? "收起高级信息 ︿" : "高级信息 ﹀"
                Accessible.name: text
                onClicked: expanded = !expanded
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                contentItem: Text {
                    text: advancedButton.expanded ? "收起高级信息" : "高级信息"
                    color: advancedButton.enabled
                           ? theme.textSecondary : theme.textDisabled
                    font.pixelSize: theme.fontMeta
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    implicitHeight: 40
                    radius: theme.radiusSmall
                    color: advancedButton.down ? theme.surfacePressed
                          : advancedButton.hovered ? theme.surfaceHover
                          : theme.surfaceRaised
                    border.width: advancedButton.activeFocus ? 2 : 1
                    border.color: advancedButton.activeFocus
                                  ? theme.focus : theme.border
                }
            }

            ColumnLayout {
                id: audioDiagnostics
                objectName: "advancedSection"
                Layout.fillWidth: true
                visible: advancedButton.expanded
                spacing: theme.spacingSm
                Text {
                    text: "高级信息"
                    color: theme.textPrimary
                    font.pixelSize: theme.fontLabel
                    font.weight: Font.DemiBold
                }
                InfoRow { Layout.fillWidth: true; label: "原始共享质量"; value: drawer.controller.screenProfile }
                InfoRow { Layout.fillWidth: true; label: "原始视频来源"; value: drawer.controller.videoSource }
                InfoRow { Layout.fillWidth: true; label: "协商编码"; value: drawer.controller.videoNegotiatedCodec }
                InfoRow { Layout.fillWidth: true; label: "编码器"; value: drawer.controller.videoEncoderImplementation }
                InfoRow { Layout.fillWidth: true; label: "硬件状态"; value: drawer.controller.videoHardwareStatus }
                InfoRow { Layout.fillWidth: true; label: "捕获配置"; value: drawer.controller.videoCaptureProfile }
                InfoRow { Layout.fillWidth: true; label: "呈交帧"; value: String(drawer.controller.presentationSubmissions) }
                InfoRow { Layout.fillWidth: true; label: "回调帧"; value: String(drawer.controller.presentationCallbacks) }
                InfoRow { Layout.fillWidth: true; label: "替换帧"; value: String(drawer.controller.presentationCoalesced) }
                InfoRow { Layout.fillWidth: true; label: "呈交延迟 P95"; value: drawer.controller.presentationDelayP95Ms + " ms" }
                InfoRow { Layout.fillWidth: true; label: "呈交延迟最大"; value: drawer.controller.presentationDelayMaxMs + " ms" }
                InfoRow { Layout.fillWidth: true; label: "音频路由代次"; value: String(drawer.controller.audioRouteGeneration) }
                InfoRow { Layout.fillWidth: true; label: "音频时钟可信度"; value: drawer.controller.audioClockConfidence }
                InfoRow { Layout.fillWidth: true; label: "渲染队列"; value: drawer.controller.audioRendererQueueDurationMs + " ms" }
                InfoRow { Layout.fillWidth: true; label: "设备队列"; value: drawer.controller.audioDeviceQueueDurationMs + " ms" }
                InfoRow { Layout.fillWidth: true; label: "音频欠载"; value: String(drawer.controller.audioUnderrunCount) }
                InfoRow { Layout.fillWidth: true; label: "音频中断"; value: drawer.controller.audioLastDiscontinuityCategory }
                InfoRow { Layout.fillWidth: true; label: "路由监视"; value: drawer.controller.audioRouteMonitorStatus }
                InfoRow { Layout.fillWidth: true; label: "播放差值"; value: drawer.controller.hostViewerDeltaMs + " ms" }
                InfoRow { Layout.fillWidth: true; label: "同步动作"; value: drawer.controller.hostSyncAction }
                InfoRow { Layout.fillWidth: true; label: "建议动作"; value: drawer.controller.viewerSuggestedAction }
                InfoRow { Layout.fillWidth: true; label: "应用动作"; value: drawer.controller.viewerAppliedAction }
                InfoRow { Layout.fillWidth: true; label: "接收播放位置"; value: drawer.controller.viewerRenderedAvailable ? drawer.controller.viewerRenderedPositionMs + " ms" : "不可用" }
                InfoRow { Layout.fillWidth: true; label: "漂移场景"; value: drawer.controller.driftScenarioActive ? drawer.controller.driftScenarioPhase : "未启用" }
            }
        }
    }
}
