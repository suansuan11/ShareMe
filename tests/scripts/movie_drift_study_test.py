#!/usr/bin/env python3

import importlib.util
import queue
import tempfile
import unittest
import time
from pathlib import Path


def load_runner():
    script = Path(__file__).parents[2] / "scripts" / "run_movie_drift_study.py"
    spec = importlib.util.spec_from_file_location("shareme_movie_drift_study", script)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load drift runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MovieDriftStudyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = load_runner()

    def test_room_discovery_accepts_only_sanitized_room_line(self):
        self.assertEqual(self.runner.parse_room_line("ROOM ABC234"), "ABC234")
        self.assertIsNone(self.runner.parse_room_line("ROOM secret-room"))
        self.assertIsNone(self.runner.parse_room_line("ROOM ABC234 path=/private/movie"))

    def test_commands_keep_host_and_viewer_roles_separate(self):
        host = self.runner.build_host_command(
            Path("/bin/demo"), "ws://127.0.0.1:18080/v1/ws",
            Path("/private/movie.mkv"), Path("/private/out.jsonl")
        )
        viewer = self.runner.build_viewer_command(
            Path("/bin/demo"), "ws://127.0.0.1:18080/v1/ws", "ABC234"
        )
        self.assertIn("--role", host)
        self.assertIn("host", host)
        self.assertIn("--metrics-jsonl", host)
        self.assertIn("--drift-scenario", host)
        self.assertIn("--role", viewer)
        self.assertIn("viewer", viewer)
        self.assertNotIn("--metrics-jsonl", viewer)
        self.assertNotIn("/private/movie.mkv", viewer)

    def test_acceptance_profile_is_exactly_three_runs(self):
        self.assertEqual(self.runner.validate_run_count(3), 3)
        with self.assertRaises(ValueError):
            self.runner.validate_run_count(2)

    def test_partial_or_failed_runs_never_count_as_complete(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.jsonl"
            path.write_text(
                '{"kind":"summary","complete":false}\n', encoding="utf-8"
            )
            self.assertFalse(self.runner.is_complete_artifact(path))
            path.write_text(
                '{"kind":"summary","complete":true}\n', encoding="utf-8"
            )
            self.assertTrue(self.runner.is_complete_artifact(path))
            self.assertFalse(self.runner.result_is_complete("RESULT drift-study-v1 status=failed"))
            self.assertFalse(self.runner.result_is_complete(""))
            self.assertTrue(
                self.runner.result_is_complete(
                    "RESULT drift-study-v1 status=complete accepted_samples=0 "
                    "rejected_samples=0 received_reports=0"
                )
            )
            counters = self.runner.parse_result_counters(
                "RESULT drift-study-v1 status=complete accepted_samples=4 "
                "rejected_samples=2 received_reports=7 "
                "report_receive_attempts=9 report_decode_successes=7"
            )
            self.assertEqual(counters["acceptedSamples"], 4)
            self.assertEqual(counters["receivedReports"], 7)
            self.assertEqual(counters["reportReceiveAttempts"], 9)
            self.assertEqual(counters["reportDecodeSuccesses"], 7)
            viewer_counters = self.runner.parse_viewer_counters(
                "DRIFT_COUNTERS role=viewer sink_submissions=12 "
                "report_encode_attempts=8 report_encode_successes=7 "
                "report_send_attempts=7 report_send_successes=6"
            )
            self.assertEqual(viewer_counters["sinkSubmissions"], 12)
            self.assertEqual(viewer_counters["reportSendSuccesses"], 6)

    def test_artifact_hash_and_output_root_refuse_escape_or_existing_targets(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            root = parent / "study"
            self.runner.prepare_output_root(root, parent)
            artifact = root / "run-01.jsonl"
            artifact.write_text("sample\n", encoding="utf-8")
            self.assertEqual(len(self.runner.sha256_file(artifact)), 64)
            with self.assertRaises(ValueError):
                self.runner.prepare_output_root(parent / ".." / "outside", parent)
            with self.assertRaises(FileExistsError):
                self.runner.refuse_existing_artifact(artifact)

    def test_summary_recomputes_percentiles_and_rejects_incomplete_input(self):
        samples = [
            {"kind": "sample", "captureTimeMs": 0, "sampleIndex": 0,
             "reportSequence": 0, "generation": 1, "deltaMs": -200,
             "phase": "steady"},
            {"kind": "sample", "captureTimeMs": 250, "sampleIndex": 1,
             "reportSequence": 1, "generation": 1, "deltaMs": 50,
             "phase": "steady"},
            {"kind": "sample", "captureTimeMs": 500, "sampleIndex": 2,
             "reportSequence": 2, "generation": 1, "deltaMs": 100,
             "phase": "post-resume"},
        ]
        summary = self.runner.recompute_summary(samples)
        self.assertEqual(summary["acceptedSamples"], 3)
        self.assertEqual(summary["absoluteP50Ms"], 100)
        self.assertEqual(summary["absoluteP95Ms"], 200)
        self.assertEqual(summary["absoluteP99Ms"], 200)
        self.assertEqual(summary["absoluteMaxMs"], 200)
        self.assertFalse(self.runner.gates_pass({"runs": []}))

        pause_samples = [
            {"kind": "sample", "captureTimeMs": 89_750, "sampleIndex": 0,
             "reportSequence": 0, "generation": 1, "deltaMs": 0,
             "phase": "steady"},
            {"kind": "sample", "captureTimeMs": 95_000, "sampleIndex": 1,
             "reportSequence": 1, "generation": 1, "deltaMs": 0,
             "phase": "post-resume"},
            {"kind": "sample", "captureTimeMs": 97_750, "sampleIndex": 2,
             "reportSequence": 2, "generation": 1, "deltaMs": 0,
             "phase": "post-resume"},
        ]
        pause_summary = self.runner.recompute_summary(
            pause_samples,
            [{"startCaptureTimeMs": 90_000, "endCaptureTimeMs": 95_000}],
        )
        self.assertEqual(pause_summary["reportGapCount"], 1)
        self.assertEqual(pause_summary["largestReportGapMs"], 2_750)

    def test_diagnostics_redact_paths_and_room_secrets(self):
        diagnostic = self.runner.redact_diagnostic(
            "ROOM ABC234 failed /private/movie.mkv /private/demo",
            ["ABC234", "/private/movie.mkv", "/private/demo"],
        )
        self.assertNotIn("ABC234", diagnostic)
        self.assertNotIn("/private/movie.mkv", diagnostic)
        self.assertNotIn("/private/demo", diagnostic)
        self.assertIn("<redacted>", diagnostic)

    def test_native_qt_platform_is_not_overridden_unless_explicit(self):
        native = self.runner.build_demo_environment({"QT_QPA_PLATFORM": "native"}, None)
        self.assertNotIn("QT_QPA_PLATFORM", native)
        offscreen = self.runner.build_demo_environment({}, "offscreen")
        self.assertEqual(offscreen["QT_QPA_PLATFORM"], "offscreen")

    def test_runner_detects_viewer_exit_before_host_result(self):
        class Process:
            def __init__(self, code):
                self.code = code

            def poll(self):
                return self.code

        self.assertTrue(self.runner.viewer_is_alive(Process(None)))
        self.assertFalse(self.runner.viewer_is_alive(Process(1)))
        self.assertTrue(
            self.runner.complete_result_requires_viewer(
                "RESULT drift-study-v1 status=complete", Process(None)
            )
        )
        self.assertFalse(
            self.runner.complete_result_requires_viewer(
                "RESULT drift-study-v1 status=complete", Process(1)
            )
        )

    def test_runner_wakes_when_viewer_exits_while_host_is_silent(self):
        class Reader:
            events = queue.Queue()

        class Process:
            def poll(self):
                return 1

        with self.assertRaisesRegex(
            self.runner.DriftStudyError, "viewer-exited-before-host-event"
        ):
            self.runner.wait_for_host_event_or_viewer(
                Reader(), Process(), time.monotonic() + 10
            )

    def test_runner_rechecks_viewer_after_host_exit(self):
        class Host:
            def poll(self):
                return 0

        class Viewer:
            def poll(self):
                return 1

        with self.assertRaisesRegex(
            self.runner.DriftStudyError, "viewer-exited-after-result"
        ):
            self.runner.wait_for_host_exit_or_viewer(Host(), Viewer(), 1)

    def test_runner_preserves_partial_artifact_failure_category(self):
        with tempfile.TemporaryDirectory() as directory:
            artifact = Path(directory) / "run-01.jsonl"
            artifact.write_text('{"kind":"summary"}\n', encoding="utf-8")
            with self.assertRaisesRegex(
                self.runner.DriftStudyError,
                "viewer-exited-before-host-event; partial-artifact=run-01.jsonl",
            ):
                self.runner.raise_run_error(
                    self.runner.DriftStudyError("viewer-exited-before-host-event"),
                    artifact,
                )


if __name__ == "__main__":
    unittest.main()
