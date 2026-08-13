#!/usr/bin/env python3

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import run_primary_voice_smoke as runner  # noqa: E402


class PrimaryVoiceSmokeTest(unittest.TestCase):
    def test_runner_disables_playout_and_never_passes_motion_fixture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            demo = root / "demo"
            demo.write_bytes(b"binary")
            artifact = root / "voice.jsonl"
            summary = {
                "host": {"voice_packets_sent": 30, "voice_packets_received": 31},
                "viewer": {"voice_packets_sent": 32, "voice_packets_received": 33},
                "primary_voice_controls_acknowledged": True,
                "primary_voice_quality_stats_available": True,
            }
            with mock.patch.object(runner, "run_screen_smoke", return_value=summary) as run:
                result = runner.run_voice_smoke(
                    demo=demo,
                    server_root=root,
                    duration_seconds=10,
                    port=19090,
                    artifact=artifact,
                )
            self.assertTrue(result["bidirectional_voice"])
            self.assertFalse(result["motion_fixture_started"])
            self.assertIsNone(run.call_args.kwargs["motion_fixture"])
            self.assertEqual(
                run.call_args.kwargs["role_environment_overrides"],
                {
                    "host": {"SHAREME_PRIMARY_VOICE_SMOKE": "1"},
                    "viewer": {"SHAREME_PRIMARY_VOICE_SMOKE": "1"},
                },
            )
            records = [json.loads(line) for line in artifact.read_text().splitlines()]
            self.assertEqual([record["kind"] for record in records], ["run", "summary"])
            self.assertFalse(records[-1]["native_audio_playout"])
            self.assertTrue(records[-1]["primary_voice_controls_acknowledged"])
            self.assertTrue(records[-1]["primary_voice_quality_stats_available"])

    def test_missing_voice_direction_fails_without_success_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            demo = root / "demo"
            demo.write_bytes(b"binary")
            artifact = root / "voice.jsonl"
            summary = {
                "host": {"voice_packets_sent": 30, "voice_packets_received": 0},
                "viewer": {"voice_packets_sent": 32, "voice_packets_received": 33},
                "primary_voice_controls_acknowledged": True,
                "primary_voice_quality_stats_available": True,
            }
            with mock.patch.object(runner, "run_screen_smoke", return_value=summary):
                with self.assertRaisesRegex(runner.VoiceSmokeError, "bidirectional"):
                    runner.run_voice_smoke(
                        demo=demo,
                        server_root=root,
                        duration_seconds=10,
                        port=19090,
                        artifact=artifact,
                    )
            self.assertFalse(artifact.exists())

    def test_missing_control_or_quality_ack_fails_closed(self):
        base = {
            "host": {"voice_packets_sent": 30, "voice_packets_received": 31},
            "viewer": {"voice_packets_sent": 32, "voice_packets_received": 33},
            "primary_voice_controls_acknowledged": True,
            "primary_voice_quality_stats_available": True,
        }
        for missing, error in (
            ("primary_voice_controls_acknowledged", "controls"),
            ("primary_voice_quality_stats_available", "quality"),
        ):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                demo = root / "demo"
                demo.write_bytes(b"binary")
                summary = dict(base)
                summary[missing] = False
                with mock.patch.object(runner, "run_screen_smoke", return_value=summary):
                    with self.assertRaisesRegex(runner.VoiceSmokeError, error):
                        runner.run_voice_smoke(
                            demo=demo, server_root=root, duration_seconds=10,
                            port=19090, artifact=root / "voice.jsonl"
                        )

    def test_rejects_overwrite_and_nonpositive_duration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            demo = root / "demo"
            demo.write_bytes(b"binary")
            artifact = root / "voice.jsonl"
            artifact.write_text("keep")
            with self.assertRaisesRegex(runner.VoiceSmokeError, "overwrite"):
                runner.run_voice_smoke(
                    demo=demo, server_root=root, duration_seconds=10,
                    port=19090, artifact=artifact
                )
            artifact.unlink()
            with self.assertRaisesRegex(runner.VoiceSmokeError, "duration"):
                runner.run_voice_smoke(
                    demo=demo, server_root=root, duration_seconds=0,
                    port=19090, artifact=artifact
                )


if __name__ == "__main__":
    unittest.main()
