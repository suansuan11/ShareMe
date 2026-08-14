#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
import unittest
from pathlib import Path


class GuiQmlContractTest(unittest.TestCase):
    demo = Path()

    def run_state(self, state: str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        return subprocess.run(
            [str(self.demo), "--gui-smoke-state", state],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
            env=environment,
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
        return subprocess.run(
            arguments,
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
            env=environment,
        )

    def assert_clean_state(self, state: str) -> None:
        result = self.run_state(state)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"GUI_STATE page={state} qml_loaded=1", result.stdout)
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

    def test_create_preflight_loads_without_qml_errors(self):
        self.assert_clean_state("create")

    def test_join_preflight_loads_without_qml_errors(self):
        self.assert_clean_state("join")

    def test_home_and_preflight_expose_primary_actions(self):
        for state, required in (
            ("home", ("createRoomButton", "joinRoomButton")),
            ("create", ("preflightPrimaryButton", "qualityProfileControl")),
            ("join", ("roomCodeField", "preflightPrimaryButton")),
        ):
            result = self.run_state(state)
            self.assertEqual(result.returncode, 0, result.stderr)
            for object_name in required:
                self.assertIn(f"GUI_OBJECT {object_name}=1", result.stdout)

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
                self.assertIn(
                    f"GUI_STATE page=call-{role} qml_loaded=1", result.stdout
                )
                self.assertNotIn("TypeError:", result.stderr)
                self.assertNotIn("ReferenceError:", result.stderr)
                self.assertNotIn("failed to load component", result.stderr)

    def test_real_qml_controls_drive_audio_drawer_and_leave(self):
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        result = subprocess.run(
            [
                str(self.demo),
                "--server", "ws://127.0.0.1:18080/v1/ws",
                "--role", "host",
                "--source", "test",
                "--audio", "synthetic",
                "--no-audio-playout",
                "--gui-smoke-state", "call-host-actions",
            ],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
            env=environment,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            "GUI_ACTION microphone=1 speaker=1 drawer=1 voice_panel=1 "
            "volume_rejected_restored=1 leave=1 page=home",
            result.stdout,
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
