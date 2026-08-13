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

    def test_capture_fault_is_injected_only_after_both_final_event_acks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            scenario = self.runner.LifecycleScenario(
                scenario="nested",
                mode="controlled",
                trigger_after_seconds=10,
                host_trigger_directory=root / "host",
                viewer_trigger_directory=root / "viewer",
                capture_error_during_evaluation=True,
            )
            host = Reader()
            viewer = Reader()
            for index, event in enumerate(scenario.required_events):
                scenario.advance(20.0, host, viewer)
                host.lines.append(
                    "SMOKE_STATUS session-lifecycle-event "
                    f"event={event} generation=1\n"
                )
                if index < len(scenario.required_events) - 1:
                    viewer.lines.append(
                        "SMOKE_STATUS session-lifecycle-event "
                        f"event={event} generation=1\n"
                    )
            scenario.advance(20.0, host, viewer)
            self.assertFalse(scenario.capture_fault_trigger.exists())
            viewer.lines.append(
                "SMOKE_STATUS session-lifecycle-event "
                "event=screen-unlocked generation=1\n"
            )
            scenario.advance(20.0, host, viewer)
            self.assertTrue(scenario.capture_fault_trigger.is_file())

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
        for sample in range(15):
            records.append({
                "role": "host",
                "submitted": 100 + sample,
                "encoded": 100 + sample,
                "bytes_sent": 10_000 + sample * 1_000,
                "voice_packets_sent": 500 + sample * 50,
                "voice_packets_received": 500 + sample * 50,
                "voice_bytes_sent": 40_000 + sample * 4_000,
                "voice_bytes_received": 40_000 + sample * 4_000,
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

        late_host = [dict(record) for record in records]
        late_viewer = [dict(record) for record in viewer_records]
        for sample in range(5, 10):
            for key in ("submitted", "encoded", "bytes_sent"):
                late_host[sample][key] = late_host[4][key]
            for key in ("submitted", "received", "decoded", "bytes_received"):
                late_viewer[sample][key] = late_viewer[4][key]
        with self.assertRaisesRegex(
            self.runner.LifecycleSmokeError, "video-recovery-timeout"
        ):
            scenario.validate(
                Reader(*host_events), Reader(*viewer_events), late_host,
                late_viewer
            )

        with self.assertRaisesRegex(
            self.runner.LifecycleSmokeError, "viewer-recovery-marker-missing"
        ):
            scenario.validate(Reader(*host_events), Reader(*viewer_events[:-1]), records,
                              viewer_records)

    def test_continuity_exclusions_require_complete_same_generation_causality(self):
        def causal_lines(generation=1, include_sleep=True, duplicate=False):
            lines = [
                "PERF_COUNTERS role=host",
                "PERF_COUNTERS role=host",
                "SMOKE_STATUS session-lifecycle-event "
                f"event=screen-locked generation={generation}",
                "PERF_COUNTERS role=host",
            ]
            if include_sleep:
                lines.extend([
                    "SMOKE_STATUS session-lifecycle-event "
                    f"event=will-sleep generation={generation}",
                    "PERF_COUNTERS role=host",
                ])
            lines.extend([
                "SMOKE_STATUS session-lifecycle-event "
                f"event=did-wake generation={generation}",
                "PERF_COUNTERS role=host",
                "SMOKE_STATUS session-lifecycle-event "
                f"event=screen-unlocked generation={generation}",
                "PERF_COUNTERS role=host",
                "SMOKE_STATUS session-lifecycle-recovered "
                f"generation={generation} decision=healthy",
            ])
            if duplicate:
                lines.append(
                    "SMOKE_STATUS session-lifecycle-recovered "
                    f"generation={generation} decision=healthy"
                )
            return lines

        def for_role(lines, role):
            return [line.replace("role=host", f"role={role}") for line in lines]

        scenario = self.runner.LifecycleScenario(
            scenario="nested", mode="physical-wait", trigger_after_seconds=10
        )
        host = causal_lines()
        viewer = for_role(causal_lines(), "viewer")
        self.assertEqual(
            scenario.continuity_exclusions(Reader(*host), Reader(*viewer)),
            {"host": [(2, 6)], "viewer": [(2, 6)]},
        )

        invalid = (
            (causal_lines(include_sleep=False), viewer, "host-lifecycle-events-missing"),
            (causal_lines(duplicate=True), viewer, "host-recovery-marker-missing"),
            (causal_lines(generation=2), viewer, "host-lifecycle-generation-mismatch"),
        )
        for host_lines, viewer_lines, failure in invalid:
            with self.subTest(failure=failure):
                with self.assertRaisesRegex(
                    self.runner.LifecycleSmokeError, failure
                ):
                    scenario.continuity_exclusions(
                        Reader(*host_lines), Reader(*viewer_lines)
                    )

    def test_physical_resume_decision_authorizes_exactly_one_capture_restart(self):
        lifecycle_events = [
            "SMOKE_STATUS session-lifecycle-event event=screen-locked generation=1",
            "SMOKE_STATUS session-lifecycle-event event=will-sleep generation=1",
            "SMOKE_STATUS session-lifecycle-event event=did-wake generation=1",
            "SMOKE_STATUS session-lifecycle-event event=screen-unlocked generation=1",
        ]
        host_lines = [
            *lifecycle_events,
            *("PERF_COUNTERS role=host" for _ in range(4)),
            "SMOKE_STATUS session-lifecycle-recovered "
            "generation=1 decision=capture-restarted",
        ]
        viewer_lines = [
            *lifecycle_events,
            *("PERF_COUNTERS role=viewer" for _ in range(4)),
            "SMOKE_STATUS session-lifecycle-recovered "
            "generation=1 decision=healthy",
        ]
        host_records = []
        for sample in range(15):
            host_records.append({
                "role": "host",
                "submitted": 100 + sample,
                "encoded": 100 + sample,
                "bytes_sent": 10_000 + sample * 1_000,
                "voice_packets_sent": 500 + sample * 50,
                "voice_packets_received": 500 + sample * 50,
                "voice_bytes_sent": 40_000 + sample * 4_000,
                "voice_bytes_received": 40_000 + sample * 4_000,
                "screen_capture_restart_attempts": int(sample >= 4),
                "screen_capture_restart_successes": int(sample >= 4),
                "screen_capture_generation": int(sample >= 4),
            })
        viewer_records = [
            {
                **record,
                "role": "viewer",
                "received": record["encoded"],
                "decoded": record["encoded"],
                "bytes_received": record["bytes_sent"],
                "screen_capture_restart_attempts": 0,
                "screen_capture_restart_successes": 0,
                "screen_capture_generation": 0,
            }
            for record in host_records
        ]
        scenario = self.runner.LifecycleScenario(
            scenario="nested", mode="physical-wait", trigger_after_seconds=10
        )

        summary = scenario.validate(
            Reader(*host_lines),
            Reader(*viewer_lines),
            host_records,
            viewer_records,
        )
        self.assertTrue(summary["capture_recovery_verified"])
        self.assertFalse(summary["healthy_call_preserved"])

    def test_failure_text_is_redacted_before_artifact_use(self):
        secret = "/Users/private/person/room ABC234 token=secret"
        redacted = self.runner.redact_lifecycle_failure(secret)
        self.assertNotIn("/Users/private", redacted)
        self.assertNotIn("ABC234", redacted)
        self.assertNotIn("secret", redacted)


if __name__ == "__main__":
    unittest.main()
