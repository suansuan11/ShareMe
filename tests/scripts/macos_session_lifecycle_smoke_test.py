#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock


def load_runner():
    script = (
        Path(__file__).parents[2]
        / "scripts"
        / "run_macos_session_lifecycle_smoke.py"
    )
    spec = importlib.util.spec_from_file_location(
        "shareme_macos_session_lifecycle_smoke", script
    )
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load lifecycle runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Reader:
    def __init__(self, *lines: str):
        self.lines = [f"{line}\n" for line in lines]


class MacosSessionLifecycleSmokeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = load_runner()

    def test_options_are_macos_bounded_and_leave_a_post_resume_window(self):
        with mock.patch.object(self.runner.sys, "platform", "darwin"):
            self.runner.validate_options("nested", "controlled", 40, 10)
            self.runner.validate_options("lock", "physical-wait", 60, 15)
            for duration, trigger_after in ((20, 10), (40, 3), (40, 29)):
                with self.subTest(duration=duration, trigger_after=trigger_after):
                    with self.assertRaises(self.runner.LifecycleSmokeError):
                        self.runner.validate_options(
                            "nested", "controlled", duration, trigger_after
                        )
        with mock.patch.object(self.runner.sys, "platform", "win32"):
            with self.assertRaisesRegex(
                self.runner.LifecycleSmokeError, "macos-only"
            ):
                self.runner.validate_options("lock", "controlled", 40, 10)

    def test_controlled_nested_sequence_waits_for_both_role_acknowledgements(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scenario = self.runner.LifecycleScenario(
                scenario="nested",
                mode="controlled",
                trigger_after_seconds=10,
                host_trigger_directory=root / "host",
                viewer_trigger_directory=root / "viewer",
            )
            host = Reader()
            viewer = Reader()
            scenario.advance(10.0, host, viewer)
            self.assertTrue((root / "host" / "0001-screen-locked.trigger").is_file())
            self.assertTrue((root / "viewer" / "0001-screen-locked.trigger").is_file())

            host.lines.append(
                "SMOKE_STATUS session-lifecycle-event event=screen-locked generation=1\n"
            )
            scenario.advance(11.0, host, viewer)
            self.assertFalse((root / "host" / "0002-will-sleep.trigger").exists())
            viewer.lines.append(
                "SMOKE_STATUS session-lifecycle-event event=screen-locked generation=1\n"
            )
            scenario.advance(11.0, host, viewer)
            self.assertTrue((root / "host" / "0002-will-sleep.trigger").is_file())

    def test_physical_mode_never_creates_trigger_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scenario = self.runner.LifecycleScenario(
                scenario="lock",
                mode="physical-wait",
                trigger_after_seconds=10,
                host_trigger_directory=root / "host",
                viewer_trigger_directory=root / "viewer",
            )
            scenario.advance(60.0, Reader(), Reader())
            self.assertEqual(list(root.rglob("*.trigger")), [])

    def test_validation_requires_both_recovery_markers_and_ten_progress_samples(self):
        recovered = (
            "SMOKE_STATUS session-lifecycle-recovered generation=1 decision=healthy"
        )
        lifecycle_events = [
            "SMOKE_STATUS session-lifecycle-event event=screen-locked generation=1",
            "SMOKE_STATUS session-lifecycle-event event=screen-unlocked generation=1",
        ]
        host_events = [*lifecycle_events]
        viewer_events = [*lifecycle_events]
        for _ in range(4):
            host_events.append("PERF_COUNTERS role=host")
            viewer_events.append("PERF_COUNTERS role=viewer")
        host_events.append(recovered)
        viewer_events.append(recovered)
        records = []
        for sample in range(14):
            records.append({
                "role": "host",
                "submitted": 100 + sample,
                "encoded": 100 + sample,
                "bytes_sent": 10_000 + sample * 1_000,
                "voice_packets_sent": 500 + sample * 50,
                "voice_packets_received": 500 + sample * 50,
                "screen_capture_restart_attempts": 0,
                "screen_capture_restart_successes": 0,
                "screen_capture_generation": 0,
            })
        viewer_records = [
            {
                **record,
                "role": "viewer",
                "received": record["encoded"],
                "decoded": record["encoded"],
                "bytes_received": record["bytes_sent"],
            }
            for record in records
        ]
        scenario = self.runner.LifecycleScenario(
            scenario="lock", mode="physical-wait", trigger_after_seconds=10
        )
        summary = scenario.validate(
            Reader(*host_events), Reader(*viewer_events), records, viewer_records
        )
        self.assertEqual(summary["post_resume_samples"], 10)
        self.assertTrue(summary["healthy_call_preserved"])

        with self.assertRaisesRegex(
            self.runner.LifecycleSmokeError, "viewer-recovery-marker-missing"
        ):
            scenario.validate(Reader(*host_events), Reader(*viewer_events[:-1]), records,
                              viewer_records)

    def test_failure_text_is_redacted_before_artifact_use(self):
        secret = "/Users/private/person/room ABC234 token=secret"
        redacted = self.runner.redact_lifecycle_failure(secret)
        self.assertNotIn("/Users/private", redacted)
        self.assertNotIn("ABC234", redacted)
        self.assertNotIn("secret", redacted)


if __name__ == "__main__":
    unittest.main()
