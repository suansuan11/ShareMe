#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
import unittest
from pathlib import Path


class RtcDemoCliTest(unittest.TestCase):
    demo = Path()
    qml = Path()
    controller_source = Path()
    movie_supported = False

    def run_demo(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.setdefault("QT_QPA_PLATFORM", "offscreen")
        return subprocess.run(
            [str(self.demo), *arguments],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
            env=environment,
        )

    def test_help_documents_sender_receiver_contract(self):
        result = self.run_demo("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("--server", result.stdout)
        self.assertIn("--role", result.stdout)
        self.assertIn("--room", result.stdout)
        self.assertIn("--source", result.stdout)
        self.assertIn("test, desktop, or movie", result.stdout)
        self.assertIn("host or viewer", result.stdout)
        self.assertIn("--movie", result.stdout)
        self.assertIn("--movie-audio", result.stdout)

    def test_missing_required_options_is_usage_error(self):
        self.assertEqual(self.run_demo().returncode, 2)
        self.assertEqual(
            self.run_demo(
                "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "bad"
            ).returncode,
            2,
        )

    def test_viewer_requires_room(self):
        result = self.run_demo(
            "--server",
            "ws://127.0.0.1:18080/v1/ws",
            "--role",
            "viewer",
        )
        self.assertEqual(result.returncode, 2)

    def test_rejects_invalid_or_viewer_desktop_source(self):
        invalid = self.run_demo(
            "--server",
            "ws://127.0.0.1:18080/v1/ws",
            "--role",
            "host",
            "--source",
            "camera",
        )
        self.assertEqual(invalid.returncode, 2)

        viewer = self.run_demo(
            "--server",
            "ws://127.0.0.1:18080/v1/ws",
            "--role",
            "viewer",
            "--room",
            "ABC234",
            "--source",
            "desktop",
        )
        self.assertEqual(viewer.returncode, 2)

    def test_movie_source_contract_and_path_redaction(self):
        movie = "/private/super-secret-movie.mp4"
        accepted = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "movie", "--movie", movie, "--movie-audio", "--validate"
        )
        self.assertEqual(accepted.returncode, 0 if self.movie_supported else 2)
        missing = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "movie"
        )
        self.assertEqual(missing.returncode, 2)
        viewer = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "viewer",
            "--room", "ABC234", "--source", "movie", "--movie", movie
        )
        self.assertEqual(viewer.returncode, 2)
        self.assertNotIn(movie, viewer.stderr)

    def test_sender_qml_exposes_bounded_host_controls(self):
        source = self.qml.read_text(encoding="utf-8")
        self.assertIn("hostControlsAvailable", source)
        self.assertIn("pauseHostPlayback()", source)
        self.assertIn("resumeHostPlayback()", source)
        self.assertIn("seekHostPlayback(", source)
        self.assertIn("to: Math.max(0, window.controller.hostPlaybackDurationMs)", source)
        self.assertIn("when: !playbackSlider.pressed", source)

    def test_controller_uses_dedicated_movie_audio_relays(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("movie-audio-session-description", source)
        self.assertIn("movie-audio-ice-candidate", source)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--qml", type=Path, required=True)
    parser.add_argument("--controller-source", type=Path, required=True)
    parser.add_argument("--movie-supported", action="store_true")
    args, unittest_args = parser.parse_known_args()
    RtcDemoCliTest.demo = args.demo.resolve()
    RtcDemoCliTest.qml = args.qml.resolve()
    RtcDemoCliTest.controller_source = args.controller_source.resolve()
    RtcDemoCliTest.movie_supported = args.movie_supported
    unittest.main(argv=[sys.argv[0], *unittest_args])
    return 0


if __name__ == "__main__":
    sys.exit(main())
