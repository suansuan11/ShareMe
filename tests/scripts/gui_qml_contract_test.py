#!/usr/bin/env python3

import argparse
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock


class GuiQmlContractTest(unittest.TestCase):
    demo = Path()

    def assert_exact_line(self, output: str, marker: str) -> None:
        self.assertIn(marker, output.splitlines())

    def run_demo(
        self, arguments: list[str], environment: dict[str, str]
    ) -> subprocess.CompletedProcess[str]:
        try:
            return subprocess.run(
                arguments,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="strict",
                timeout=5,
                check=False,
                env=environment,
            )
        except UnicodeDecodeError:
            self.fail("GUI output decode failed")

    def run_state(
        self, state: str, compact: bool = False
    ) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = (
            "offscreen:size=760x520" if compact else "offscreen"
        )
        return self.run_demo(
            [str(self.demo), "--gui-smoke-state", state], environment
        )

    def assert_sanitized_output(
        self, result: subprocess.CompletedProcess[str]
    ) -> None:
        output = []
        for line in (result.stdout + "\n" + result.stderr).splitlines():
            if line.startswith("GUI_RECOVERY_TITLE category="):
                line = re.sub(r" category=\S+", "", line, count=1)
            output.append(line)
        visible_output = "\n".join(output)
        for raw in ("kVTParameterErr", "HRESULT", "NSError", "ICE", "SDP"):
            with self.subTest(raw=raw):
                self.assertNotIn(raw, visible_output)
        self.assertNotRegex(
            visible_output,
            r"(?<![A-Za-z0-9:/])/(?:[A-Za-z0-9_.-]+/){1,}[A-Za-z0-9_.-]+",
        )
        self.assertNotRegex(
            visible_output,
            r"(?i)(?:password|passwd|secret|token|credential|api[-_]?key)"
            r"\s*[:=]\s*\S+",
        )

    def run_call_state(self, role: str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        arguments = [
            str(self.demo),
            "--server", "ws://127.0.0.1:18080/v1/ws",
            "--role", role,
            "--source", "test",
            "--audio", "synthetic",
            "--no-audio-playout",
            "--gui-smoke-state", f"call-{role}",
        ]
        if role == "viewer":
            arguments.extend(["--room", "ABC234"])
        return self.run_demo(arguments, environment)

    def test_gui_decode_failure_is_a_sanitized_contract_failure(self):
        decode_error = UnicodeDecodeError(
            "utf-8", b"\xff", 0, 1, "invalid start byte"
        )
        with mock.patch.object(
            subprocess, "run", side_effect=decode_error
        ):
            with self.assertRaisesRegex(
                AssertionError, "GUI output decode failed"
            ):
                self.run_state("home")

    def assert_clean_state(self, state: str) -> None:
        result = self.run_state(state)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_exact_line(
            result.stdout, f"GUI_STATE page={state} qml_loaded=1"
        )
        for failure in (
            "TypeError:",
            "ReferenceError:",
            "is not a type",
            "failed to load component",
            "Binding loop",
        ):
            self.assertNotIn(failure, result.stderr)

    def test_home_loads_without_qml_errors(self):
        self.assert_clean_state("home")

    def test_shared_visual_primitives_are_registered(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        for filename in ("IconGlyph.qml", "DialogSurface.qml"):
            self.assertTrue((qml_dir / filename).is_file())
        cmake = (qml_dir.parent / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("qml/IconGlyph.qml", cmake)
        self.assertIn("qml/DialogSurface.qml", cmake)

    def test_settings_help_and_recovery_surfaces_load_cleanly(self):
        for state, object_name in (
            ("settings", "settingsDialog"),
            ("help", "helpDialog"),
            ("recovery", "recoverySurface"),
        ):
            with self.subTest(state=state):
                result = self.run_state(state)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assert_exact_line(
                    result.stdout, f"GUI_STATE page={state} qml_loaded=1"
                )
                self.assert_exact_line(
                    result.stdout, f"GUI_OBJECT {object_name}=1"
                )
                for failure in (
                    "TypeError:",
                    "ReferenceError:",
                    "is not a type",
                    "failed to load component",
                    "Binding loop",
                ):
                    self.assertNotIn(failure, result.stderr)

    def test_details_drawer_exposes_semantic_sections_and_safe_status_copy(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        source = (qml_dir / "CallDetailsDrawer.qml").read_text(encoding="utf-8")
        for object_name in (
            "connectionSection",
            "videoSection",
            "audioSection",
            "advancedSection",
        ):
            with self.subTest(object_name=object_name):
                self.assertIn(f'objectName: "{object_name}"', source)
        for title in ("连接", "画面", "声音", "高级信息"):
            with self.subTest(title=title):
                self.assertIn(f'text: "{title}"', source)
        self.assertIn("property bool expanded: false", source)
        self.assertIn("visible: advancedButton.expanded", source)
        self.assertNotIn("value: drawer.controller.status", source)
        advanced_start = source.index('objectName: "advancedSection"')
        advanced_source = source[advanced_start:]
        for marker in (
            "videoNegotiatedCodec",
            "videoEncoderImplementation",
            "videoHardwareStatus",
            "videoCaptureProfile",
            "presentationSubmissions",
            "audioRouteGeneration",
            "hostViewerDeltaMs",
        ):
            with self.subTest(advanced_marker=marker):
                self.assertIn(marker, advanced_source)
                self.assertNotIn(marker, source[:advanced_start])
        for marker in (
            "function statusLabel(status)",
            "drawer.controller.voiceQualityMessage",
            "drawer.controller.microphoneLevel",
            "speakerVolumeControl",
            "pauseHostPlayback()",
            "resumeHostPlayback()",
            "seekHostPlayback(",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)

    def test_details_normalizes_video_enums_and_keeps_raw_values_advanced(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        source = (qml_dir / "CallDetailsDrawer.qml").read_text(encoding="utf-8")
        video_start = source.index('id: videoSection')
        advanced_start = source.index('objectName: "advancedSection"')
        normal_video = source[video_start:advanced_start]
        for raw_binding in (
            "value: drawer.controller.screenProfile",
            "value: drawer.controller.videoSource",
        ):
            self.assertNotIn(raw_binding, normal_video)
        for marker in (
            "function profileLabel(profile)",
            "function sourceLabel(source)",
            'return "1080p 60 · 流畅"',
            'return "1440p 60 · 高画质"',
            'return "4K 30 · 影院"',
            'return "屏幕共享"',
            'return "桌面共享"',
            'return "影片"',
            'return "测试画面"',
            'return "不可用"',
            "drawer.profileLabel(drawer.controller.screenProfile)",
            "drawer.sourceLabel(drawer.controller.videoSource)",
            'label: "原始共享质量"',
            'label: "原始视频来源"',
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)

    def test_details_and_recovery_use_shared_theme_tokens(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        for filename in ("CallDetailsDrawer.qml", "RecoveryDialog.qml"):
            source = (qml_dir / filename).read_text(encoding="utf-8")
            with self.subTest(filename=filename):
                self.assertNotRegex(source, r"spacing:\s*\d+")
                self.assertNotRegex(source, r"anchors\.margins:\s*\d+")
                self.assertNotRegex(source, r"font\.pixelSize:\s*\d+")
                self.assertNotRegex(source, r"radius:\s*\d+")
                self.assertNotRegex(source, r'color:\s*"#')
        recovery = (qml_dir / "RecoveryDialog.qml").read_text(encoding="utf-8")
        self.assertIn("theme.lineHeightBody", recovery)

    def test_movie_seek_control_has_accessible_themed_hit_area(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        source = (qml_dir / "CallDetailsDrawer.qml").read_text(encoding="utf-8")
        playback_start = source.index("id: playbackSection")
        seek_start = source.index("Slider {", playback_start)
        seek_control = source[seek_start:source.index("            }", seek_start)]
        for marker in (
            'Accessible.name: "播放进度"',
            "implicitHeight: theme.controlHeight",
            "Layout.minimumHeight: theme.controlHeight",
            "value: Math.max(0, drawer.controller.hostPlaybackPositionMs",
            "onMoved: drawer.controller.seekHostPlayback(",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, seek_control)

    def test_secondary_dialogs_and_recovery_keep_the_approved_copy_contract(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        settings = (qml_dir / "SettingsDialog.qml").read_text(encoding="utf-8")
        help_source = (qml_dir / "HelpDialog.qml").read_text(encoding="utf-8")
        recovery = (qml_dir / "RecoveryDialog.qml").read_text(encoding="utf-8")

        for marker in (
            "DialogSurface {",
            'objectName: "settingsDialog"',
            'text: "连接地址"',
            "serverUrl",
            "onServerUrlChanged",
            "开发环境",
            "不会和房间数据一起保存",
        ):
            with self.subTest(surface="settings", marker=marker):
                self.assertIn(marker, settings)
        self.assertNotIn("设备切换", settings)

        for marker in (
            "DialogSurface {",
            'objectName: "helpDialog"',
            "怎么创建房间？",
            "怎么加入房间？",
            "没有画面怎么办？",
            "没有声音怎么办？",
            "macOS 屏幕录制权限在哪里？",
            "Tab",
            "Enter / Space",
            "Esc",
        ):
            with self.subTest(surface="help", marker=marker):
                self.assertIn(marker, help_source)

        for marker in (
            'objectName: "recoverySurface"',
            "smokePreview",
            "需要检查权限",
            "无法加入这个房间",
            "屏幕共享不可用",
            "声音设备不可用",
            "连接未建立",
            "通话暂时无法继续",
            "errorMessage",
            "returnHome()",
            "retryCall()",
        ):
            with self.subTest(surface="recovery", marker=marker):
                self.assertIn(marker, recovery)
        self.assertNotIn("诊断类别", recovery)

    def test_recovery_smoke_prints_runtime_sanitized_titles(self):
        result = self.run_state("recovery")
        self.assertEqual(result.returncode, 0, result.stderr)
        for category, title in (
            ("permission-denied", "需要检查权限"),
            ("invalid-room", "无法加入这个房间"),
            ("screen-capture", "屏幕共享不可用"),
            ("audio-device", "声音设备不可用"),
            ("connection-lost", "连接未建立"),
            ("ICE-failed", "连接未建立"),
            ("timed out", "连接未建立"),
            ("generic-failure", "通话暂时无法继续"),
        ):
            with self.subTest(category=category):
                self.assert_exact_line(
                    result.stdout,
                    f"GUI_RECOVERY_TITLE category={category} title={title}",
                )

    def test_normal_gui_output_is_sanitized(self):
        for state in (
            "home", "create", "join", "settings", "help",
            "call-host", "call-viewer", "recovery",
        ):
            with self.subTest(state=state):
                arguments = (self.run_call_state(
                    "host" if state == "call-host" else "viewer"
                ) if state.startswith("call-") else self.run_state(state))
                self.assertEqual(arguments.returncode, 0, arguments.stderr)
                self.assert_sanitized_output(arguments)

    def test_compact_minimum_window_state_exits_cleanly(self):
        result = self.run_state("create", compact=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_exact_line(
            result.stdout, "GUI_STATE page=create qml_loaded=1"
        )
        for object_name in (
            "microphoneIntentControl",
            "speakerIntentControl",
            "qualityProfileControl",
            "preflightPrimaryButton",
        ):
            self.assert_exact_line(
                result.stdout, f"GUI_OBJECT {object_name}=1"
            )
        self.assert_sanitized_output(result)

        main_source = (Path(__file__).parents[2] /
                       "client" / "tools" / "rtc_demo" / "qml" /
                       "Main.qml").read_text(encoding="utf-8")
        self.assertIn("minimumWidth: 760", main_source)
        self.assertIn("minimumHeight: 520", main_source)

    def test_create_preflight_loads_without_qml_errors(self):
        self.assert_clean_state("create")

    def test_join_preflight_loads_without_qml_errors(self):
        self.assert_clean_state("join")

    def test_home_and_preflight_expose_primary_actions(self):
        for state, required in (
            ("home", ("createRoomButton", "joinRoomButton")),
            ("create", ("preflightPrimaryButton", "qualityProfileControl",
                         "microphoneIntentControl", "speakerIntentControl")),
            ("join", ("roomCodeField", "preflightPrimaryButton",
                       "microphoneIntentControl", "speakerIntentControl")),
        ):
            result = self.run_state(state)
            self.assertEqual(result.returncode, 0, result.stderr)
            for object_name in required:
                self.assert_exact_line(
                    result.stdout, f"GUI_OBJECT {object_name}=1"
                )

    def test_home_and_preflight_use_user_facing_copy(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        sources = "\n".join(
            (qml_dir / filename).read_text(encoding="utf-8")
            for filename in ("HomePage.qml", "PreflightPage.qml")
        )
        for text in (
            "创建房间",
            "加入房间",
            "房间",
            "设备",
            "共享质量",
            "1080p 60 · 流畅",
            "1440p 60 · 高画质",
            "4K 30 · 影院",
        ):
            with self.subTest(text=text):
                self.assertIn(text, sources)

    def test_preflight_controls_use_shared_interaction_states(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        source = (qml_dir / "PreflightPage.qml").read_text(encoding="utf-8")
        for marker in (
            "function interactionSurfaceColor",
            "function interactionBorderColor",
            "property bool pointerPressed: false",
            "TapHandler",
            "hoverEnabled: true",
            "roomCodeField.pointerPressed",
            "qualityProfileControl.down",
            "microphoneIntentControl.down",
            "speakerIntentControl.down",
            "theme.surfaceHover",
            "theme.surfacePressed",
            "theme.surfaceDisabled",
            "theme.focus",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, source)

    def test_unknown_state_fails_closed(self):
        result = self.run_state("unknown")
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("GUI_STATE", result.stdout)

    def test_host_and_viewer_call_pages_load_cleanly(self):
        for role in ("host", "viewer"):
            with self.subTest(role=role):
                result = self.run_call_state(role)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assert_exact_line(
                    result.stdout, f"GUI_STATE page=call-{role} qml_loaded=1"
                )
                self.assertNotIn("TypeError:", result.stderr)
                self.assertNotIn("ReferenceError:", result.stderr)
                self.assertNotIn("failed to load component", result.stderr)
                self.assert_exact_line(
                    result.stdout, "GUI_OBJECT callPage=1"
                )
                self.assert_exact_line(
                    result.stdout, "GUI_OBJECT microphoneControl=1"
                )
                self.assert_exact_line(
                    result.stdout, "GUI_OBJECT speakerControl=1"
                )
                self.assert_exact_line(
                    result.stdout, "GUI_OBJECT detailsControl=1"
                )
                self.assert_exact_line(
                    result.stdout, "GUI_OBJECT leaveControl=1"
                )
                self.assert_exact_line(
                    result.stdout, "GUI_OBJECT shareControl=0"
                )
                for object_name in (
                    "connectionSection",
                    "videoSection",
                    "audioSection",
                    "advancedSection",
                ):
                    self.assert_exact_line(
                        result.stdout, f"GUI_OBJECT {object_name}=1"
                    )

    def test_call_stage_overlay_visibility_is_mutually_exclusive(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        source = (qml_dir / "VideoStage.qml").read_text(encoding="utf-8")

        status_start = source.index("id: statusRow")
        status_visibility_start = source.index("visible:", status_start)
        status_visibility = source[
            status_visibility_start:source.index("        Rectangle {", status_visibility_start)
        ]
        message_start = source.index("id: stageMessage")
        message_visibility_start = source.index("visible:", message_start)
        message_visibility = source[
            message_visibility_start:source.index("        Text {", message_visibility_start)
        ]

        for marker in (
            "!stage.sessionTransition",
            "&& !stage.captureRecovering",
            "stage.controller.viewer",
            "stage.controller.remoteVideoAvailable",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, status_visibility)
        for marker in (
            "|| (!stage.captureRecovering",
            "!stage.sessionTransition",
            "!stage.controller.remoteVideoAvailable",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, message_visibility)

    def test_call_room_copy_control_uses_semantic_interaction_states(self):
        qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
        source = (qml_dir / "CallTopBar.qml").read_text(encoding="utf-8")
        room_start = source.index("id: roomButton")
        room_control = source[room_start:]
        for marker in (
            "implicitHeight: theme.controlHeight",
            "hoverEnabled: true",
            "Accessible.name:",
            "Accessible.description:",
            "ToolTip.text:",
            "ToolTip.visible: (hovered || activeFocus)",
            "roomButton.down",
            "!roomButton.enabled",
            "theme.surfacePressed",
            "theme.surfaceDisabled",
            "theme.textDisabled",
            "theme.accentHover",
            "theme.accentPressed",
            "theme.focus",
        ):
            with self.subTest(marker=marker):
                self.assertIn(marker, room_control)

    def test_real_qml_controls_drive_audio_drawer_and_leave(self):
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        result = self.run_demo(
            [
                str(self.demo),
                "--server", "ws://127.0.0.1:18080/v1/ws",
                "--role", "host",
                "--source", "test",
                "--audio", "synthetic",
                "--no-audio-playout",
                "--gui-smoke-state", "call-host-actions",
            ],
            environment,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_exact_line(
            result.stdout,
            "GUI_ACTION microphone=1 speaker=1 drawer=1 voice_panel=1 "
            "volume_rejected_restored=1 leave=1 page=home",
        )
        self.assert_exact_line(
            result.stdout, "GUI_ACTION advanced_closed=1 advanced_expanded=1"
        )
        self.assertNotIn("TypeError:", result.stderr)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path, required=True)
    args, unittest_args = parser.parse_known_args()
    GuiQmlContractTest.demo = args.demo.resolve()
    unittest.main(argv=[sys.argv[0], *unittest_args])
    return 0


if __name__ == "__main__":
    sys.exit(main())
