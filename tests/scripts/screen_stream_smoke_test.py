#!/usr/bin/env python3

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock


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
            Path("demo"), "ws://127.0.0.1:18080/v1/ws", "quality", "auto"
        )
        self.assertIn("--source", host)
        self.assertIn("screen", host)
        self.assertIn("--screen-profile", host)
        self.assertIn("quality", host)
        self.assertIn("--screen-encoder", host)
        self.assertIn("auto", host)
        self.assertEqual(host[-3:], ["--audio", "synthetic", "--no-audio-playout"])

        software = self.runner.build_host_command(
            Path("demo"), "ws://127.0.0.1:18080/v1/ws", "standard", "software"
        )
        self.assertIn("software", software)

        viewer = self.runner.build_viewer_command(
            Path("demo"), "ws://127.0.0.1:18080/v1/ws", "ABC234"
        )
        self.assertIn("ABC234", viewer)
        self.assertIn("--source", viewer)
        self.assertIn("screen", viewer)
        self.assertEqual(viewer[-3:], ["--audio", "synthetic", "--no-audio-playout"])

    def test_guard_process_exit_fails_the_measurement(self):
        class ExitedProcess:
            def poll(self):
                return 3

        with self.assertRaisesRegex(self.runner.SmokeRuntimeError,
                                    "fixture-early-exit"):
            self.runner.require_guard_processes_alive(
                (("fixture", ExitedProcess()),)
            )

    def test_motion_fixture_command_and_process_are_bounded(self):
        command = self.runner.build_motion_fixture_command(
            Path("fixture"), "standard", 30
        )
        self.assertEqual(
            command,
            ["fixture", "--profile", "standard", "--duration-seconds", "60"],
        )
        self.assertEqual(
            self.runner.build_motion_fixture_command(
                Path("fixture"), "cinema", 3590
            )[-1],
            "3600",
        )

        environment = {"QT_QPA_PLATFORM": "offscreen", "KEEP": "value"}
        process = object()
        with mock.patch.object(
                self.runner, "_start_measured_demo", return_value=process
        ) as start:
            self.assertIs(
                self.runner.start_motion_fixture(
                    Path("fixture"), "quality", 120, environment
                ),
                process,
            )
        options = start.call_args.kwargs
        self.assertEqual(
            start.call_args.args[0],
            ["fixture", "--profile", "quality", "--duration-seconds", "150"],
        )
        self.assertEqual(options["stdout"], self.runner.subprocess.DEVNULL)
        self.assertEqual(options["stderr"], self.runner.subprocess.DEVNULL)
        self.assertNotIn("QT_QPA_PLATFORM", options["env"])
        self.assertEqual(options["env"]["KEEP"], "value")

    def test_failed_run_still_records_an_independent_run_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = root / "demo"
            demo.write_bytes(b"demo")
            artifact = root / "run.jsonl"
            with mock.patch.object(
                    self.runner, "start_signaling_server",
                    side_effect=OSError("injected")):
                with self.assertRaises(self.runner.SmokeRuntimeError):
                    self.runner.run_smoke(
                        demo=demo,
                        server_root=root,
                        profile="standard",
                        duration_seconds=1,
                        port=18080,
                        artifact=artifact,
                    )
            first = json.loads(artifact.read_text(encoding="utf-8").splitlines()[0])
            self.assertRegex(first["run_id"], r"^[0-9a-f]{32}$")

    def test_owned_motion_fixture_is_cleaned_and_redacted_on_failure(self):
        class FixtureProcess:
            def __init__(self):
                self.returncode = None

            def poll(self):
                return self.returncode

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = root / "demo"
            demo.write_bytes(b"demo")
            fixture = root / "secret-motion-fixture"
            fixture.write_bytes(b"fixture")
            artifact = root / "run.jsonl"
            process = FixtureProcess()

            def terminate(target, grace_seconds):
                self.assertIs(target, process)
                self.assertEqual(grace_seconds, 1)
                target.returncode = 0

            with (
                mock.patch.object(
                    self.runner, "start_motion_fixture", return_value=process
                ) as start,
                mock.patch.object(self.runner, "wait_for_motion_fixture_ready"),
                mock.patch.object(
                    self.runner, "start_signaling_server",
                    side_effect=OSError("injected"),
                ),
                mock.patch.object(
                    self.runner, "terminate_process_group",
                    side_effect=terminate,
                ) as stop,
            ):
                with self.assertRaises(self.runner.SmokeRuntimeError):
                    self.runner.run_smoke(
                        demo=demo,
                        server_root=root,
                        profile="standard",
                        duration_seconds=1,
                        port=18080,
                        artifact=artifact,
                        motion_fixture=fixture,
                    )

            start.assert_called_once()
            stop.assert_called_once()
            records = [
                json.loads(line)
                for line in artifact.read_text(encoding="utf-8").splitlines()
            ]
            self.assertTrue(records[0]["motion_fixture_requested"])
            self.assertTrue(records[-1]["motion_fixture_started"])
            self.assertTrue(records[-1]["motion_fixture_stopped"])
            self.assertNotIn(str(fixture), artifact.read_text(encoding="utf-8"))

    def test_missing_owned_motion_fixture_is_rejected_before_artifact_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = root / "demo"
            demo.write_bytes(b"demo")
            artifact = root / "run.jsonl"
            with self.assertRaisesRegex(
                    self.runner.SmokeRuntimeError, "motion-fixture-unavailable"
            ):
                self.runner.run_smoke(
                    demo=demo,
                    server_root=root,
                    profile="standard",
                    duration_seconds=1,
                    port=18080,
                    artifact=artifact,
                    motion_fixture=root / "missing",
                )
            self.assertFalse(artifact.exists())

    def test_cli_forwards_the_owned_motion_fixture(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            demo = root / "demo"
            demo.write_bytes(b"demo")
            fixture = root / "fixture"
            fixture.write_bytes(b"fixture")
            artifact = root / "run.jsonl"
            arguments = [
                "run_screen_stream_smoke.py",
                "--demo", str(demo),
                "--server-root", str(root),
                "--profile", "standard",
                "--duration-seconds", "30",
                "--artifact", str(artifact),
                "--motion-fixture", str(fixture),
            ]
            with (
                mock.patch.object(self.runner.sys, "argv", arguments),
                mock.patch.object(
                    self.runner, "run_smoke", return_value={"complete": True}
                ) as run,
                mock.patch("builtins.print"),
            ):
                self.assertEqual(self.runner.main(), 0)
            self.assertEqual(
                run.call_args.kwargs["motion_fixture"], fixture.resolve()
            )

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
        windows_diagnostic = self.runner.redact_diagnostic(
            r"failed C:\Users\Alice\private\capture.dll"
        )
        self.assertNotIn("Alice", windows_diagnostic)
        self.assertNotIn("capture.dll", windows_diagnostic)

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
            "encoder_implementation": (
                "MediaFoundation"
                if self.runner.sys.platform == "win32"
                else "VideoToolbox"
            ),
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
            "encoder_implementation": (
                "MediaFoundation"
                if self.runner.sys.platform == "win32"
                else "VideoToolbox"
            ),
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
            "encoder_implementation": "VP8Template",
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
