#!/usr/bin/env python3

import importlib.util
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


def _load_runner():
    script = Path(__file__).parents[2] / "scripts" / "run_windows_gui_acceptance.py"
    spec = importlib.util.spec_from_file_location("windows_gui_acceptance", script)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class WindowsGuiAcceptanceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = _load_runner()
        cls.fixture = Path(os.environ["SHAREME_MOTION_FIXTURE"]) \
            if os.environ.get("SHAREME_MOTION_FIXTURE") else None

    def make_program(self, directory: Path, body: str) -> Path:
        program = directory / "program.py"
        program.write_text("#!/usr/bin/env python3\n" + body, encoding="utf-8")
        program.chmod(program.stat().st_mode | stat.S_IXUSR)
        return program

    def test_rejects_invalid_profile_and_duration(self):
        with self.assertRaisesRegex(ValueError, "invalid-profile"):
            self.runner.validate_fixture_arguments("fast", 2)
        with self.assertRaisesRegex(ValueError, "invalid-duration"):
            self.runner.validate_fixture_arguments("standard", 0)

    def test_fixture_offscreen_smoke_and_cli_rejection(self):
        if self.fixture is None:
            self.skipTest("configured fixture executable required")
        environment = os.environ.copy()
        environment["QT_QPA_PLATFORM"] = "offscreen"
        passed = subprocess.run(
            [str(self.fixture), "--profile", "standard",
             "--duration-seconds", "1"],
            capture_output=True, text=True, check=False, env=environment,
        )
        self.assertEqual(passed.returncode, 0, passed.stderr)
        self.assertRegex(
            passed.stdout,
            r"SCREEN_MOTION_FIXTURE status=completed profile=standard frames=\d+",
        )
        invalid = subprocess.run(
            [str(self.fixture), "--profile", "invalid",
             "--duration-seconds", "1"],
            capture_output=True, text=True, check=False, env=environment,
        )
        self.assertEqual(invalid.returncode, 2)

    def test_manual_checklist_names_every_surface_and_scale(self):
        checklist = self.runner.build_manual_checklist(100)
        self.assertEqual(
            [item["surface"] for item in checklist["surfaces"]],
            ["home", "create", "join", "settings", "help", "call",
             "details", "recovery"],
        )
        self.assertEqual(
            [item["scalePercent"] for item in checklist["dpiScales"]],
            [100, 125, 150, 200],
        )
        self.assertEqual(checklist["dpiScales"][0]["status"], "available")
        self.assertTrue(all(
            item["status"] == "environment-dependent"
            for item in checklist["dpiScales"][1:]
        ))

    def test_probe_states_cover_secondary_surfaces(self):
        self.assertEqual(
            self.runner.PROBE_STATES,
            ("home", "create", "join", "settings", "help", "recovery",
             "call-host", "call-viewer", "call-host-actions"),
        )

    def test_windows_probe_count_matches_gui_smoke_contract(self):
        self.assertEqual(
            self.runner.PROBE_STATES, self.runner.GUI_PROBE_STATES
        )
        self.assertEqual(
            len(self.runner.PROBE_STATES), self.runner.GUI_SMOKE_PROBE_COUNT
        )

    def test_artifact_is_redacted_and_failure_is_atomic(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            artifact = root / "acceptance.json"
            secret = str(root / "private" / "shareme.exe")
            payload = self.runner.acceptance_artifact(
                probes=[{"state": "home", "passed": True,
                         "duration_ms": 3}],
                fixture={"profile": "standard", "frames": 60},
                process_summary={"sampleCount": 2, "cpuMeanPercent": 1.0,
                                 "cpuMaxPercent": 1.5, "rssMaxKiB": 1024},
                dpi_percent=100,
            )
            self.runner.atomic_write_json(artifact, payload)
            encoded = artifact.read_text(encoding="utf-8")
            self.assertNotIn(secret, encoded)
            self.assertEqual(json.loads(encoded)["schema"],
                             "windows-gui-acceptance-v1")

            with self.assertRaisesRegex(RuntimeError, "injected"):
                self.runner.atomic_write_json(
                    artifact, {"status": "new"},
                    before_replace=lambda: (_ for _ in ()).throw(
                        RuntimeError("injected")
                    ),
                )
            self.assertEqual(json.loads(artifact.read_text())["schema"],
                             "windows-gui-acceptance-v1")
            self.assertEqual(list(root.glob("*.tmp")), [])

    def test_probe_failure_preserves_completed_probe_results(self):
        probe_type = self.runner.dataclasses.make_dataclass(
            "Probe", [("state", str), ("passed", bool), ("duration_ms", int)]
        )
        completed = probe_type("home", True, 7)
        error = self.runner.GuiSmokeFailure("probe-exit:create", [completed])
        payload = self.runner.failure_artifact(error, [])
        self.assertEqual(payload["failure"], "probe-exit:create")
        self.assertEqual(payload["probes"], [
            {"state": "home", "passed": True, "duration_ms": 7}
        ])


if __name__ == "__main__":
    unittest.main()
