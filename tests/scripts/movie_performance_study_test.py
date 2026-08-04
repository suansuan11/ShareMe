#!/usr/bin/env python3

import importlib.util
import json
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
            "max_pending=1 "
            "width=3840 height=2160 cadence_num=24 cadence_den=1 "
            "pixel_aspect_num=1 pixel_aspect_den=1 color_range=limited "
            "color_space=bt2020nc codec=hevc profile=main10 "
            "path=software state=playing candidate=host"
        )
        self.assertEqual(parsed["width"], 3840)
        self.assertEqual(parsed["path"], "software")
        self.assertEqual(parsed["dropped"], 0)
        self.assertEqual(parsed["max_pending"], 1)
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
        self.assertEqual(self.runner.scenario_phase(0), "warmup")
        self.assertEqual(self.runner.scenario_phase(30), "measurement")
        self.assertEqual(self.runner.scenario_phase(149), "measurement")
        self.assertEqual(self.runner.scenario_phase(150), "finalization")

    def test_aggregator_keeps_hashes_and_fails_missing_quality_metrics(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def write_artifact(path, cpu, mode, demo_sha256, width=3840,
                               max_pending=1, measurement_cpu=None):
                records = [{"kind": "run", "version": 1, "mode": mode,
                            "demo_sha256": demo_sha256}]
                for elapsed in range(180):
                    phase = self.runner.scenario_phase(elapsed)
                    sample_cpu = cpu if measurement_cpu is None else (
                        measurement_cpu if phase == "measurement" else cpu
                    )
                    for role in ("host", "viewer"):
                        records.append({
                            "kind": "process", "elapsed_seconds": elapsed,
                            "phase": phase, "role": role,
                            "cpu_percent": sample_cpu if role == "host" else 10,
                            "rss_bytes": 100,
                        })
                        records.append({
                            "kind": "counter", "elapsed_seconds": elapsed,
                            "phase": phase, "role": role, "decoded": 100,
                            "received": 100, "submitted": 100,
                            "coalesced": 0, "dropped": 0,
                            "conversion_failures": 0, "fallback_copies": 0,
                            "max_pending": max_pending,
                            "width": width, "height": 2160,
                            "cadence_num": 24000, "cadence_den": 1001,
                            "pixel_aspect_num": 1, "pixel_aspect_den": 1,
                            "color_range": "limited" if role == "host" else "unknown",
                            "color_space": "unknown", "codec": "hevc" if role == "host" else "unknown",
                            "profile": "Main10" if role == "host" else "unknown",
                            "path": mode,
                        })
                records.append({"kind": "summary", "complete": True,
                                "failure": None, "counter_count": 360,
                                "platform": "darwin"})
                path.write_text("\n".join(json.dumps(r) for r in records) + "\n",
                                encoding="utf-8")

            baseline = []
            candidate = []
            for index in range(3):
                baseline_path = root / f"baseline-{index}.jsonl"
                candidate_path = root / f"candidate-{index}.jsonl"
                write_artifact(baseline_path, 100, "software", "b" * 64)
                write_artifact(candidate_path, 60, "auto", "c" * 64)
                baseline.append(baseline_path)
                candidate.append(candidate_path)
            report = self.runner.aggregate_performance_runs(baseline, candidate)
            self.assertEqual(len(report["baselineRuns"]), 3)
            self.assertEqual(len(report["candidateRuns"]), 3)
            self.assertTrue(report["exact_dimensions"])
            self.assertTrue(report["exact_metadata"])
            self.assertEqual(report["additional_drops"], 0)
            self.assertIsNone(report["psnr_db"])
            self.assertFalse(report["gatePassed"])
            self.assertRegex(report["baselineRuns"][0]["sha256"], r"^[0-9a-f]{64}$")

    def test_aggregator_rejects_shared_demo_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            paths = []
            for index in range(6):
                path = root / f"run-{index}.jsonl"
                path.write_text(
                    json.dumps({"kind": "run", "demo_sha256": "a" * 64}) + "\n"
                    + json.dumps({"kind": "summary", "complete": True}) + "\n",
                    encoding="utf-8",
                )
                paths.append(path)
            with self.assertRaisesRegex(ValueError, "demo identities"):
                self.runner.aggregate_performance_runs(paths[:3], paths[3:])

    def test_summary_uses_only_contiguous_measurement_process_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.jsonl"
            records = [
                {"kind": "run", "demo_sha256": "b" * 64},
                {"kind": "summary", "complete": True},
            ]
            for phase, cpu in (("warmup", 100), ("measurement", 20),
                               ("finalization", 100)):
                elapsed = {"warmup": 0, "measurement": 30,
                           "finalization": 150}[phase]
                records.insert(-1, {"kind": "process", "role": "host",
                                    "phase": phase, "elapsed_seconds": elapsed,
                                    "cpu_percent": cpu, "rss_bytes": 100})
                records.insert(-1, {"kind": "process", "role": "viewer",
                                    "phase": phase, "elapsed_seconds": elapsed,
                                    "cpu_percent": cpu, "rss_bytes": 100})
            path.write_text("\n".join(json.dumps(r) for r in records) + "\n",
                            encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "measurement samples"):
                self.runner.summarize_performance_artifact(path)

    def test_summary_rejects_noncontiguous_measurement_counter_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.jsonl"
            records = [{"kind": "run", "demo_sha256": "c" * 64}]
            for elapsed in range(180):
                phase = self.runner.scenario_phase(elapsed)
                for role in ("host", "viewer"):
                    records.append({"kind": "process", "role": role,
                                    "phase": phase, "elapsed_seconds": elapsed,
                                    "cpu_percent": 10, "rss_bytes": 100})
            for role in ("host", "viewer"):
                for _ in range(120):
                    records.append({"kind": "counter", "role": role,
                                    "phase": "measurement", "elapsed_seconds": 30,
                                    "max_pending": 1})
            records.append({"kind": "summary", "complete": True})
            path.write_text("\n".join(json.dumps(r) for r in records) + "\n",
                            encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "counter samples"):
                self.runner.summarize_performance_artifact(path)

    def test_process_metric_parser_rejects_missing_or_zero_rss_samples(self):
        with self.assertRaises(ValueError):
            self.runner.parse_process_metrics("", 1)
        with self.assertRaises(ValueError):
            self.runner.parse_process_metrics("0.0 0", 0)

    def test_aggregator_checks_each_run_and_role_and_backlog_depth(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)

            def write(path, mode, sha, width, max_pending):
                records = [{"kind": "run", "demo_sha256": sha,
                            "mode": mode}]
                for elapsed in range(180):
                    phase = self.runner.scenario_phase(elapsed)
                    for role in ("host", "viewer"):
                        records.append({"kind": "process", "role": role,
                                        "phase": phase, "elapsed_seconds": elapsed,
                                        "cpu_percent": 10, "rss_bytes": 100})
                        records.append({"kind": "counter", "role": role,
                                        "phase": phase, "elapsed_seconds": elapsed,
                                        "submitted": 100, "dropped": 0,
                                        "coalesced": 0, "max_pending": max_pending,
                                        "width": width, "height": 2160,
                                        "cadence_num": 24000, "cadence_den": 1001,
                                        "pixel_aspect_num": 1, "pixel_aspect_den": 1,
                                        "color_range": "limited" if role == "host" else "unknown",
                                        "color_space": "unknown", "codec": "hevc" if role == "host" else "unknown",
                                        "profile": "Main10" if role == "host" else "unknown"})
                records.append({"kind": "summary", "complete": True})
                path.write_text("\n".join(json.dumps(r) for r in records) + "\n",
                                encoding="utf-8")

            baseline = []
            candidate = []
            for index in range(3):
                base = root / f"base-{index}.jsonl"
                cand = root / f"cand-{index}.jsonl"
                write(base, "software", "b" * 64, 3840, 1)
                write(cand, "auto", "c" * 64, 853 if index == 0 else 3840, 2)
                baseline.append(base)
                candidate.append(cand)
            report = self.runner.aggregate_performance_runs(baseline, candidate)
            self.assertFalse(report["exact_dimensions"])
            self.assertFalse(report["one_frame_backlog_bound"])
            self.assertEqual(report["run_role_comparisons"][0]["run"], 1)
            self.assertFalse(report["run_role_comparisons"][0]["roles"]["host"]["exact_dimensions"])


if __name__ == "__main__":
    unittest.main()
