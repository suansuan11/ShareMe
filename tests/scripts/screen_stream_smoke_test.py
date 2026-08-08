#!/usr/bin/env python3

import importlib.util
import unittest
from pathlib import Path


def load_runner():
    script = Path(__file__).parents[2] / "scripts" / "run_screen_stream_smoke.py"
    spec = importlib.util.spec_from_file_location("shareme_screen_smoke", script)
    if spec is None or spec.loader is None:
        raise RuntimeError("could not load screen smoke runner")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ScreenStreamSmokeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runner = load_runner()

    def test_profile_bounds_and_commands(self):
        self.assertEqual(self.runner.profile_bounds("standard"), (1920, 1080, 60))
        self.assertEqual(self.runner.profile_bounds("quality"), (2560, 1440, 60))
        self.assertEqual(self.runner.profile_bounds("cinema"), (3840, 2160, 30))

        host = self.runner.build_host_command(
            Path("demo"), "ws://127.0.0.1:18080/v1/ws", "quality"
        )
        self.assertIn("--source", host)
        self.assertIn("screen", host)
        self.assertIn("--screen-profile", host)
        self.assertIn("quality", host)

        viewer = self.runner.build_viewer_command(
            Path("demo"), "ws://127.0.0.1:18080/v1/ws", "ABC234"
        )
        self.assertIn("ABC234", viewer)
        self.assertIn("--source", viewer)
        self.assertIn("screen", viewer)

    def test_parses_sanitized_counter_lines(self):
        parsed = self.runner.parse_counter_line(
            "PERF_COUNTERS version=1 role=host cpu_percent=0 rss_bytes=0 "
            "width=2560 height=1440 "
            "encoded=20 callback=20 submitted=20 received=20 decoded=20 "
            "bytes_sent=10000 bitrate_bps=8000 max_pending=1 "
            "conversion_failures=0 fallback_copies=0 offered=20 coalesced=0 "
            "dropped=0 stats_unavailable=0 state=playing candidate=unknown "
            "requested_mode=software decoder_path=software codec=unknown "
            "webrtc_encoder=H264 hardware_encoder_status=active "
            "encoder_implementation=VideoToolbox"
        )
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed["hardware_encoder_status"], "active")
        self.assertEqual(parsed["bitrate_bps"], 8000)
        self.assertIsNone(
            self.runner.parse_counter_line(
                "PERF_COUNTERS version=1 role=host path=/private/secret"
            )
        )
        diagnostic = self.runner.redact_diagnostic(
            "failed /private/secret ROOM ABC234 token-value"
        )
        self.assertNotIn("/private/secret", diagnostic)
        self.assertNotIn("ABC234", diagnostic)

    def test_profile_validation_requires_hardware_and_bounded_media(self):
        host = {
            "role": "host",
            "width": 1920,
            "height": 1080,
            "encoded": 20,
            "callback": 20,
            "submitted": 20,
            "bytes_sent": 10000,
            "bitrate_bps": 8000,
            "max_pending": 1,
            "conversion_failures": 0,
            "fallback_copies": 0,
            "webrtc_encoder": "H264",
            "hardware_encoder_status": "active",
        }
        viewer = {
            "role": "viewer",
            "width": 1920,
            "height": 1080,
            "received": 20,
            "decoded": 20,
            "callback": 20,
            "submitted": 20,
            "bytes_received": 10000,
            "bitrate_bps": 8000,
            "max_pending": 1,
            "conversion_failures": 0,
            "fallback_copies": 0,
        }
        terminal_host = dict(host, bitrate_bps=0)
        terminal_viewer = dict(viewer, bitrate_bps=0)
        result = self.runner.validate_records(
            "standard", [host, terminal_host], [viewer, terminal_viewer]
        )
        self.assertEqual(result["profile"], "standard")
        self.assertEqual(result["hardware_encoder_status"], "active")
        self.assertEqual(result["host"]["bitrate_bps"], 8000)
        self.assertEqual(result["viewer"]["bitrate_bps"], 8000)

        host["hardware_encoder_status"] = "fallback:probe-rejected"
        with self.assertRaises(self.runner.SmokeRuntimeError):
            self.runner.validate_records("standard", [host], [viewer])


if __name__ == "__main__":
    unittest.main()
