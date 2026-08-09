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
        self.assertEqual(host[-3:], ["--audio", "synthetic", "--no-audio-playout"])

        viewer = self.runner.build_viewer_command(
            Path("demo"), "ws://127.0.0.1:18080/v1/ws", "ABC234"
        )
        self.assertIn("ABC234", viewer)
        self.assertIn("--source", viewer)
        self.assertIn("screen", viewer)
        self.assertEqual(viewer[-3:], ["--audio", "synthetic", "--no-audio-playout"])

    def test_parses_sanitized_counter_lines(self):
        parsed = self.runner.parse_counter_line(
            "PERF_COUNTERS version=1 role=host cpu_percent=0 rss_bytes=0 "
            "width=2560 height=1440 "
            "encoded=20 callback=20 submitted=20 received=20 decoded=20 "
            "voice_packets_sent=30 voice_packets_received=31 "
            "voice_bytes_sent=3000 voice_bytes_received=3100 "
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
            "voice_packets_sent": 20,
            "voice_packets_received": 21,
            "voice_bytes_sent": 2000,
            "voice_bytes_received": 2100,
            "stats_unavailable": 0,
            "presentation_epoch": 0,
            "presentation_recovery_count": 0,
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
            "voice_packets_sent": 22,
            "voice_packets_received": 23,
            "voice_bytes_sent": 2200,
            "voice_bytes_received": 2300,
            "stats_unavailable": 0,
            "presentation_epoch": 0,
            "presentation_recovery_count": 0,
        }
        terminal_host = dict(
            host,
            encoded=40,
            callback=40,
            submitted=40,
            bitrate_bps=0,
            voice_packets_sent=40,
            voice_packets_received=41,
            voice_bytes_sent=4000,
            voice_bytes_received=4100,
        )
        terminal_viewer = dict(
            viewer,
            received=40,
            decoded=40,
            callback=40,
            submitted=40,
            bitrate_bps=0,
            voice_packets_sent=42,
            voice_packets_received=43,
            voice_bytes_sent=4200,
            voice_bytes_received=4300,
            presentation_epoch=1,
            presentation_recovery_count=1,
        )
        result = self.runner.validate_records(
            "standard", [host, terminal_host], [viewer, terminal_viewer]
        )
        self.assertEqual(result["profile"], "standard")
        self.assertEqual(result["hardware_encoder_status"], "active")
        self.assertEqual(result["host"]["bitrate_bps"], 8000)
        self.assertEqual(result["viewer"]["bitrate_bps"], 8000)
        self.assertEqual(result["host"]["voice_packets_sent"], 40)
        self.assertEqual(result["viewer"]["voice_packets_received"], 43)
        self.assertEqual(result["viewer"]["presentation_recovery_count"], 1)

        host["hardware_encoder_status"] = "fallback:probe-rejected"
        with self.assertRaises(self.runner.SmokeRuntimeError):
            self.runner.validate_records("standard", [host], [viewer])

    def test_validation_rejects_geometry_mismatch_and_counter_stalls(self):
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
            "voice_packets_sent": 20,
            "voice_packets_received": 20,
            "voice_bytes_sent": 2000,
            "voice_bytes_received": 2000,
            "stats_unavailable": 0,
            "presentation_epoch": 0,
            "presentation_recovery_count": 0,
        }
        viewer = {
            "role": "viewer",
            "width": 1918,
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
            "voice_packets_sent": 20,
            "voice_packets_received": 20,
            "voice_bytes_sent": 2000,
            "voice_bytes_received": 2000,
            "stats_unavailable": 0,
            "presentation_epoch": 1,
            "presentation_recovery_count": 1,
        }
        with self.assertRaisesRegex(self.runner.SmokeRuntimeError, "geometry"):
            self.runner.validate_records("standard", [host], [viewer])

        viewer["width"] = 1920
        stalled_hosts = [dict(host) for _ in range(8)]
        stalled_viewers = [dict(viewer) for _ in range(8)]
        with self.assertRaisesRegex(self.runner.SmokeRuntimeError, "stalled"):
            self.runner.validate_records("standard", stalled_hosts, stalled_viewers)

    def test_windows_software_fallback_keeps_media_and_voice_gates(self):
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
            "webrtc_encoder": "VP8",
            "hardware_encoder_status": "fallback:platform-unavailable",
            "voice_packets_sent": 20,
            "voice_packets_received": 21,
            "voice_bytes_sent": 2000,
            "voice_bytes_received": 2100,
            "stats_unavailable": 0,
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
            "voice_packets_sent": 22,
            "voice_packets_received": 23,
            "voice_bytes_sent": 2200,
            "voice_bytes_received": 2300,
            "stats_unavailable": 0,
            "presentation_epoch": 0,
            "presentation_recovery_count": 0,
        }
        terminal_host = dict(
            host,
            encoded=40,
            callback=40,
            submitted=40,
            bitrate_bps=0,
            voice_packets_sent=40,
            voice_packets_received=41,
            voice_bytes_sent=4000,
            voice_bytes_received=4100,
        )
        terminal_viewer = dict(
            viewer,
            received=40,
            decoded=40,
            callback=40,
            submitted=40,
            bitrate_bps=0,
            voice_packets_sent=42,
            voice_packets_received=43,
            voice_bytes_sent=4200,
            voice_bytes_received=4300,
            presentation_epoch=1,
            presentation_recovery_count=1,
        )
        host_records = [host, terminal_host]
        viewer_records = [viewer, terminal_viewer]
        result = self.runner.validate_records(
            "quality",
            host_records,
            viewer_records,
            require_hardware=False,
        )
        self.assertEqual(result["webrtc_encoder"], "VP8")
        self.assertEqual(
            result["hardware_encoder_status"],
            "fallback:platform-unavailable",
        )

        terminal_host["webrtc_encoder"] = "H264"
        with self.assertRaises(self.runner.SmokeRuntimeError):
            self.runner.validate_records(
                "standard",
                host_records,
                viewer_records,
                require_hardware=False,
            )

        terminal_host["webrtc_encoder"] = "VP8"
        for record in host_records + viewer_records:
            record["width"] = 2560
            record["height"] = 1440
        with self.assertRaises(self.runner.SmokeRuntimeError):
            self.runner.validate_records(
                "quality",
                host_records,
                viewer_records,
                require_hardware=False,
            )

    def test_continuity_boundary_rejects_missing_and_regressing_voice(self):
        record = {
            "role": "host",
            "encoded": 20,
            "callback": 20,
            "submitted": 20,
            "voice_packets_sent": 20,
            "voice_packets_received": 20,
            "voice_bytes_sent": 2000,
            "voice_bytes_received": 2000,
            "stats_unavailable": 0,
        }
        five_stalls = [dict(record) for _ in range(6)]
        accepted = self.runner._validate_continuous_progress(
            five_stalls, "host", ("encoded", "callback", "submitted")
        )
        self.assertEqual(accepted["max_stall_samples"], 5)

        missing = dict(record)
        del missing["voice_packets_received"]
        with self.assertRaisesRegex(self.runner.SmokeRuntimeError, "never became ready"):
            self.runner._validate_continuous_progress(
                [missing], "host", ("encoded", "callback", "submitted")
            )

        regressed = dict(record, voice_packets_sent=19)
        with self.assertRaisesRegex(self.runner.SmokeRuntimeError, "regressed"):
            self.runner._validate_continuous_progress(
                [record, regressed],
                "host",
                ("encoded", "callback", "submitted"),
            )


if __name__ == "__main__":
    unittest.main()
