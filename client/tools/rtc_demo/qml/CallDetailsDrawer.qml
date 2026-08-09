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
    radius: 14
    ShareMeTheme { id: theme }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 18
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 13
            Text {
                text: "通话详情"
                color: theme.textPrimary
                font.pixelSize: 17
                font.weight: Font.Bold
            }
            InfoRow { Layout.fillWidth: true; label: "角色"; value: drawer.controller.viewer ? "参与者" : "主持人" }
            InfoRow { Layout.fillWidth: true; label: "状态"; value: drawer.controller.status }
            InfoRow { Layout.fillWidth: true; label: "共享质量"; value: drawer.controller.screenProfile }
            InfoRow { Layout.fillWidth: true; label: "视频来源"; value: drawer.controller.videoSource }
            InfoRow { Layout.fillWidth: true; label: "麦克风"; value: drawer.controller.microphoneMuted ? "已静音" : "已开启" }
            InfoRow { Layout.fillWidth: true; label: "扬声器"; value: drawer.controller.speakerMuted ? "已关闭" : "已开启" }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }

            ColumnLayout {
                Layout.fillWidth: true
                visible: !drawer.controller.viewer && drawer.controller.hostControlsAvailable
                spacing: 8
                Text { text: "媒体播放"; color: theme.textPrimary; font.pixelSize: 13; font.weight: Font.DemiBold }
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
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
                Slider {
                    Layout.fillWidth: true
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
                Layout.fillWidth: true
                property bool expanded: false
                text: expanded ? "收起高级诊断 ︿" : "高级诊断 ﹀"
                Accessible.name: text
                onClicked: expanded = !expanded
            }

            ColumnLayout {
                id: audioDiagnostics
                Layout.fillWidth: true
                visible: advancedButton.expanded
                spacing: 8
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
