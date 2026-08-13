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

    def test_create_preflight_loads_without_qml_errors(self):
        self.assert_clean_state("create")

    def test_join_preflight_loads_without_qml_errors(self):
        self.assert_clean_state("join")

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
            "leave=1 page=home",
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
