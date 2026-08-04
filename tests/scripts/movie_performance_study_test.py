#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
from pathlib import Path


def load_runner():
    script = Path(__file__).parents[2] / "scripts" / "run_movie_performance_study.py"
    spec = importlib.util.spec_from_file_location("shareme_movie_performance", script)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load performance runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MoviePerformanceStudyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = load_runner()

    def test_requires_three_sequential_runs_and_refuses_existing_output(self):
        self.assertEqual(self.runner.validate_run_count(3), 3)
        with self.assertRaises(ValueError):
            self.runner.validate_run_count(2)
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            root = self.runner.prepare_output_root(parent / "study", parent)
            artifact = root / "run-01.jsonl"
            artifact.write_text("existing\n", encoding="utf-8")
            with self.assertRaises(FileExistsError):
                self.runner.refuse_existing_artifact(artifact)
            with self.assertRaises(ValueError):
                self.runner.prepare_output_root(parent / ".." / "outside", parent)

    def test_parses_only_sanitized_performance_counter_lines(self):
        parsed = self.runner.parse_perf_counters(
            "PERF_COUNTERS version=1 role=viewer cpu_percent=12.5 rss_bytes=42 "
            "decoded=10 offered=10 encoded=0 received=10 callback=10 "
            "submitted=9 coalesced=1 dropped=0 conversion_failures=0 "
            "width=3840 height=2160 cadence_num=24 cadence_den=1 "
            "pixel_aspect_num=1 pixel_aspect_den=1 color_range=limited "
            "color_space=bt2020nc codec=hevc profile=main10 "
            "path=software state=playing candidate=host"
        )
        self.assertEqual(parsed["width"], 3840)
        self.assertEqual(parsed["path"], "software")
        self.assertEqual(parsed["dropped"], 0)
        self.assertEqual(self.runner.parse_perf_counters("ROOM ABC234"), None)
        self.assertEqual(
            self.runner.parse_perf_counters("PERF_COUNTERS version=1 path=/private/movie"),
            None,
        )

    def test_atomic_complete_artifact_and_sanitized_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.jsonl"
            path.write_text('{"kind":"summary","complete":true}\n', encoding="utf-8")
            self.assertTrue(self.runner.is_complete_artifact(path))
        diagnostic = self.runner.redact_diagnostic(
            "failed /private/movie.mkv ROOM ABC234 token secret",
            ["/private/movie.mkv", "ABC234", "token", "secret"],
        )
        self.assertNotIn("/private/movie.mkv", diagnostic)
        self.assertNotIn("ABC234", diagnostic)
        self.assertNotIn("token", diagnostic)

    def test_quality_and_performance_gates_require_every_frozen_condition(self):
        passing = self.runner.synthetic_passing_report()
        self.assertTrue(self.runner.gates_pass(passing))
        for field, value in (
            ("exact_dimensions", False),
            ("exact_metadata", False),
            ("cadence_ratio", 0.98),
            ("additional_drops", 1),
            ("psnr_db", 44.9),
            ("ssim", 0.994),
            ("combined_average_cpu_reduction", 0.29),
            ("candidate_cpu_p95_regression", 0.01),
            ("rss_p95_growth", 0.11),
            ("paused_cpu_reduction", 0.69),
            ("one_frame_backlog_bound", False),
        ):
            failed = dict(passing)
            failed[field] = value
            self.assertFalse(self.runner.gates_pass(failed), field)

    def test_real_session_commands_are_movie_only_and_software_is_explicit(self):
        host = self.runner.build_host_command(
            Path("/bin/demo"), "ws://127.0.0.1:18080/v1/ws",
            Path("<MOVIE_PATH>"), "software"
        )
        self.assertIn("--video-acceleration", host)
        self.assertEqual(host[host.index("--video-acceleration") + 1], "software")
        self.assertNotIn("--drift-scenario", host)
        viewer = self.runner.build_viewer_command(
            Path("/bin/demo"), "ws://127.0.0.1:18080/v1/ws", "ABC234"
        )
        self.assertEqual(viewer[-2:], ["--source", "test"])


if __name__ == "__main__":
    unittest.main()
