#!/usr/bin/env python3

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


def load_runner():
    script = Path(__file__).parents[2] / "scripts" / "run_windows_screen_acceptance.py"
    spec = importlib.util.spec_from_file_location("windows_screen_acceptance", script)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader
    spec.loader.exec_module(module)
    return module


class WindowsScreenAcceptanceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = load_runner()

    def test_modes_map_to_strict_encoder_requests(self):
        self.assertEqual(
            self.runner.screen_encoder_for_mode("software-baseline"),
            "software",
        )
        self.assertEqual(self.runner.screen_encoder_for_mode("hardware"), "auto")
        with self.assertRaisesRegex(ValueError, "invalid-mode"):
            self.runner.screen_encoder_for_mode("fallback")
        with self.assertRaisesRegex(ValueError, "software-standard-only"):
            self.runner.validate_run_request("software-baseline", "quality", 180)
        with self.assertRaisesRegex(ValueError, "invalid-duration"):
            self.runner.validate_run_request("hardware", "standard", 30)

    def test_measurement_window_percentile_and_three_run_rules(self):
        samples = [
            {"elapsed_seconds": second, "cpu_percent": float(second),
             "rss_bytes": second * 1024}
            for second in range(0, 181)
        ]
        selected = self.runner.measurement_window(samples)
        self.assertEqual(selected[0]["elapsed_seconds"], 30)
        self.assertEqual(selected[-1]["elapsed_seconds"], 150)
        self.assertEqual(self.runner.percentile([1, 2, 3, 4], 95), 4)
        self.assertEqual(self.runner.median_of_three([3.0, 1.0, 2.0]), 2.0)
        with self.assertRaisesRegex(self.runner.AcceptanceError,
                                    "measurement-window-incomplete"):
            self.runner.measurement_window(samples[31:149])
        with self.assertRaisesRegex(self.runner.AcceptanceError,
                                    "three-runs-required"):
            self.runner.median_of_three([1.0, 2.0])

    def test_summarizes_complete_mode_truthful_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "run.jsonl"
            records = [{
                "kind": "run", "version": 1, "profile": "standard",
                "screen_encoder": "software", "demo_sha256": "a" * 64,
            }]
            for role in ("host", "viewer"):
                records.extend({
                    "kind": "process", "role": role,
                    "elapsed_seconds": second,
                    "cpu_percent": 10.0 if role == "host" else 2.0,
                    "rss_bytes": 1000 if role == "host" else 500,
                } for second in range(30, 151))
            records.append({
                "kind": "summary", "complete": True, "profile": "standard",
                "webrtc_encoder": "VP8",
                "encoder_implementation": "VP8Template",
                "hardware_encoder_status": "fallback:explicit-software",
                "host": {"width": 1920, "height": 1080, "callback": 100,
                         "submitted": 99, "encoded": 98},
                "viewer": {"width": 1920, "height": 1080, "received": 98,
                           "decoded": 97, "submitted": 96,
                           "presentation_recovery_count": 1,
                           "voice_packets_sent": 10,
                           "voice_packets_received": 10},
            })
            records.append({
                "kind": "acceptance", "fixture_started": True,
                "fixture_stopped": True,
            })
            artifact.write_text("".join(
                json.dumps(record, separators=(",", ":")) + "\n"
                for record in records
            ), encoding="utf-8")
            summary = self.runner.summarize_run(artifact, "software-baseline")
            self.assertEqual(summary["host"]["cpuMean"], 10.0)
            self.assertEqual(summary["host"]["cpuP95"], 10.0)
            self.assertEqual(summary["host"]["rssP95"], 1000)
            self.assertGreaterEqual(summary["cadenceRatio"], 0.95)
            self.assertEqual(summary["demoSha256"], "a" * 64)

            artifact.write_text("".join(
                json.dumps(record, separators=(",", ":")) + "\n"
                for record in records[:-1]
            ), encoding="utf-8")
            with self.assertRaisesRegex(self.runner.AcceptanceError,
                                        "fixture-lifecycle-incomplete"):
                self.runner.summarize_run(artifact, "software-baseline")

    def test_comparison_requires_matching_binary_and_all_frozen_gates(self):
        def run(cpu, cpu_p95, rss, cadence=1.0, digest="a" * 64):
            return {
                "demoSha256": digest,
                "profile": "standard",
                "host": {"cpuMean": cpu, "cpuP95": cpu_p95, "rssP95": rss},
                "cadenceRatio": cadence,
                "qualityPassed": True,
            }

        baseline = [run(20.0, 25.0, 1000) for _ in range(3)]
        hardware = [run(12.0, 24.0, 1100) for _ in range(3)]
        comparison = self.runner.compare_standard(baseline, hardware)
        self.assertTrue(comparison["accepted"])
        self.assertAlmostEqual(comparison["cpuReduction"], 0.4)
        hardware[2] = run(12.0, 24.0, 1100, digest="b" * 64)
        with self.assertRaisesRegex(self.runner.AcceptanceError,
                                    "binary-identity-mismatch"):
            self.runner.compare_standard(baseline, hardware)

    def test_artifact_path_is_bounded_and_existing_files_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            allowed = Path(temporary) / "out"
            allowed.mkdir()
            target = allowed / "run.jsonl"
            self.runner.validate_artifact_path(target, allowed)
            target.write_text("owned", encoding="utf-8")
            with self.assertRaisesRegex(self.runner.AcceptanceError,
                                        "artifact-exists"):
                self.runner.validate_artifact_path(target, allowed)
            with self.assertRaisesRegex(self.runner.AcceptanceError,
                                        "artifact-outside-output-root"):
                self.runner.validate_artifact_path(
                    Path(temporary) / "escape.jsonl", allowed
                )
            comparison = allowed / "comparison.json"
            self.runner.validate_comparison_path(comparison, allowed)
            comparison.write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(self.runner.AcceptanceError,
                                        "comparison-exists"):
                self.runner.validate_comparison_path(comparison, allowed)

    def test_atomic_append_preserves_original_when_replace_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            artifact = Path(temporary) / "run.jsonl"
            original = '{"kind":"run"}\n'
            artifact.write_text(original, encoding="utf-8")

            with mock.patch.object(
                    self.runner.os, "replace", side_effect=OSError("blocked")):
                with self.assertRaisesRegex(OSError, "blocked"):
                    self.runner.atomic_append_jsonl(
                        artifact,
                        {"kind": "acceptance", "fixture_stopped": True},
                    )

            self.assertEqual(artifact.read_text(encoding="utf-8"), original)
            self.assertEqual(list(Path(temporary).glob("*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
