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
    controller_header = Path()
    peer_source = Path()
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
        self.assertIn("--video-acceleration", result.stdout)
        self.assertIn("--metrics-jsonl", result.stdout)
        self.assertIn("--drift-scenario", result.stdout)
        self.assertIn("--measurement-duration-seconds", result.stdout)

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

    def test_movie_video_acceleration_defaults_to_software_and_is_host_only(self):
        common = [
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "movie", "--movie", "/private/movie.mkv",
        ]
        cli_source = (self.controller_source.parent / "main.cpp").read_text(
            encoding="utf-8"
        )
        option_start = cli_source.index(
            "QCommandLineOption video_acceleration_option("
        )
        option_end = cli_source.index(
            "QCommandLineOption metrics_option", option_start
        )
        option_source = cli_source[option_start:option_end]
        self.assertIn(
            'QStringLiteral("mode"), QStringLiteral("software")', option_source
        )
        if not self.movie_supported:
            self.skipTest("video acceleration validation requires MovieRTC")
        omitted = self.run_demo(*common, "--validate")
        self.assertEqual(omitted.returncode, 0)
        automatic = self.run_demo(
            *common, "--video-acceleration", "auto", "--validate"
        )
        self.assertEqual(automatic.returncode, 0)
        software = self.run_demo(
            *common, "--video-acceleration", "software", "--validate"
        )
        self.assertEqual(software.returncode, 0)
        invalid = self.run_demo(*common, "--video-acceleration", "hardware",
                                "--validate")
        self.assertEqual(invalid.returncode, 2)
        viewer = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "viewer",
            "--room", "ABC234", "--source", "test",
            "--video-acceleration", "software", "--validate"
        )
        self.assertEqual(viewer.returncode, 2)

    def test_metrics_capture_is_host_movie_only_and_rejects_empty_or_same_path(self):
        output = "/tmp/shareme-drift-study.jsonl"
        viewer = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "viewer",
            "--room", "ABC234", "--source", "test", "--metrics-jsonl", output
        )
        self.assertEqual(viewer.returncode, 2)

        non_movie = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "test", "--metrics-jsonl", output, "--validate"
        )
        self.assertEqual(non_movie.returncode, 2)

        empty = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "movie", "--movie", "/private/movie.mkv",
            "--metrics-jsonl", "", "--validate"
        )
        self.assertEqual(empty.returncode, 2)

        same_path = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "movie", "--movie", "/private/movie.mkv",
            "--metrics-jsonl", "/private/movie.mkv", "--validate"
        )
        self.assertEqual(same_path.returncode, 2)

    def test_drift_scenario_is_frozen_host_movie_profile(self):
        common = [
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "movie", "--movie", "/private/movie.mkv",
        ]
        missing_duration = self.run_demo(
            *common, "--drift-scenario", "drift-study-v1", "--validate"
        )
        self.assertEqual(missing_duration.returncode, 2)

        wrong_duration = self.run_demo(
            *common, "--drift-scenario", "drift-study-v1",
            "--measurement-duration-seconds", "299", "--validate"
        )
        self.assertEqual(wrong_duration.returncode, 2)

        wrong_profile = self.run_demo(
            *common, "--drift-scenario", "other",
            "--measurement-duration-seconds", "300", "--validate"
        )
        self.assertEqual(wrong_profile.returncode, 2)

        viewer = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "viewer",
            "--room", "ABC234", "--source", "test", "--drift-scenario",
            "drift-study-v1", "--measurement-duration-seconds", "300"
        )
        self.assertEqual(viewer.returncode, 2)

    def test_sender_qml_exposes_bounded_host_controls(self):
        source = self.qml.read_text(encoding="utf-8")
        self.assertIn("hostControlsAvailable", source)
        self.assertIn("pauseHostPlayback()", source)
        self.assertIn("resumeHostPlayback()", source)
        self.assertIn("seekHostPlayback(", source)
        self.assertIn("to: Math.max(0, window.controller.hostPlaybackDurationMs)", source)
        self.assertIn("when: !playbackSlider.pressed", source)
        self.assertIn("driftScenarioActive", source)
        self.assertIn("driftScenarioPhase", source)

    def test_controller_uses_dedicated_movie_audio_relays(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("movie-audio-session-description", source)
        self.assertIn("movie-audio-ice-candidate", source)

    def test_controller_uses_app_owned_movie_renderer_and_preserves_voice_path(self):
        source = self.controller_source.read_text(encoding="utf-8")
        peer = self.peer_source.read_text(encoding="utf-8")
        self.assertIn("MovieAudioRenderer", source)
        self.assertIn("renderer->try_enqueue", source)
        self.assertIn("movie_audio_renderer_->pump", source)
        self.assertIn("movie_audio_renderer_->set_playback_anchor", source)
        self.assertIn("movie_audio_renderer_->snapshot", source)
        self.assertIn("movie_config.native_playout = false", source)
        self.assertIn("MovieAudioPeer::create", source)
        self.assertIn("SignaledPeer::create", source)
        self.assertIn('"host-voice"', peer)
        self.assertIn('"viewer-voice"', peer)
        self.assertIn("SetAudioPlayout(false)", peer)

    def test_controller_keeps_video_correction_observational(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("MovieVideoPlayoutSchedulerConfig::observational()", source)
        self.assertIn("activation.status", source)
        self.assertIn("movie_audio_output_ready_ = false", source)
        self.assertIn("movie-audio-output-activation-failed", source)
        self.assertIn("if (movie_audio_renderer_ && movie_audio_output_ready_)", source)
        self.assertNotIn(".apply_policy = true", source)

    def test_controller_preserves_local_output_failure_status(self):
        source = self.controller_source.read_text(encoding="utf-8")
        header = self.controller_header.read_text(encoding="utf-8")
        self.assertIn("bool movie_audio_output_ready_", header)
        self.assertNotIn("bool movie_audio_output_ready = true;", source)
        start = source.index("waiter_ = std::jthread")
        end = source.index("\nvoid RtcDemoController::stopPeer()", start)
        waiter = source[start:end]
        self.assertIn("if (!result.error.empty())", waiter)
        self.assertIn("} else if (movie_audio_output_ready_)", waiter)
        self.assertIn("if (movie_audio_output_ready_)", waiter)
        self.assertIn('setStatus(QStringLiteral("connected"))', waiter)

    def test_controller_teardown_is_dependency_safe(self):
        source = self.controller_source.read_text(encoding="utf-8")
        start = source.index("void RtcDemoController::stopPeer()")
        end = source.index("\nvoid RtcDemoController::flushDriftMetrics", start)
        teardown = source[start:end]
        ordered_markers = [
            "shutting_down_ = true",
            "movie_audio_renderer_->close_ingress",
            "movie_video_playout_adapter_->close_ingress",
            "movie_audio_pump_timer_.stop",
            "movie_audio_renderer_->quiesce_output",
            "movie_audio_renderer_->shutdown",
            "movie_video_playout_adapter_->shutdown",
            "movie_peer_->stop",
            "peer_->stop",
            "movie_audio_renderer_.reset",
            "movie_video_playout_adapter_.reset",
        ]
        positions = [teardown.index(marker) for marker in ordered_markers]
        self.assertEqual(positions, sorted(positions))

    def test_controller_routes_host_local_and_viewer_remote_video(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("config.local_video_frame", source)
        self.assertIn("if (role_ == shareme::rtc::SignaledRole::host)", source)
        self.assertIn("config.remote_video_frame", source)
        self.assertIn("if (role_ == shareme::rtc::SignaledRole::viewer)", source)

    def test_controller_records_remote_dimensions_for_performance_counters(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("performance_frame_width_.store", source)
        self.assertIn("performance_frame_height_.store", source)

    def test_performance_stats_do_not_block_counter_timer(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("performance_stats_worker_", source)
        self.assertIn("performance_stats_mutex_", source)
        start = source.index("void RtcDemoController::emitPerformanceCounters()")
        end = source.index("\nvoid RtcDemoController::stopDriftMetrics()", start)
        self.assertNotIn("peer_->video_stats()", source[start:end])

    def test_control_message_send_does_not_block_the_qt_thread(self):
        source = self.peer_source.read_text(encoding="utf-8")
        controller = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("queue_control_message(", controller)
        self.assertIn("bool queue_control_message(", source)
        self.assertIn("std::function<void(bool)>", source)
        start = source.index("bool queue_control_message(")
        end = source.index("SignaledVideoStats video_stats()", start)
        method = source[start:end]
        self.assertIn("PostTask", method)
        self.assertNotIn("BlockingCall", method)

    def test_video_stats_poll_does_not_block_on_stats_schedule(self):
        source = self.peer_source.read_text(encoding="utf-8")
        start = source.index("SignaledVideoStats video_stats()")
        end = source.index("SignaledPeerResult wait", start)
        method = source[start:end]
        self.assertIn("PostTask", method)
        self.assertNotIn("BlockingCall", method)

    def test_controller_error_notification_does_not_block_before_exit(self):
        source = self.controller_source.read_text(encoding="utf-8")
        start = source.index("void RtcDemoController::recordDriftError")
        end = source.index("\nvoid RtcDemoController::publishPlaybackState", start)
        method = source[start:end]
        self.assertIn("queue_control_message", method)
        self.assertNotIn("send_control_message", method)
        self.assertIn("QTimer::singleShot", method)

    def test_movie_sender_preserves_resolution_and_framerate(self):
        controller = self.controller_source.read_text(encoding="utf-8")
        peer = self.peer_source.read_text(encoding="utf-8")
        self.assertIn("preserve_video_quality", controller)
        self.assertIn("MAINTAIN_FRAMERATE_AND_RESOLUTION", peer)
        self.assertIn("SetParameters", peer)

    def test_controller_reports_rendered_playout_by_generation(self):
        source = self.controller_source.read_text(encoding="utf-8")
        qml = self.qml.read_text(encoding="utf-8")
        self.assertIn("publishPlayoutReport", source)
        self.assertIn("playback_anchor->video_anchor_media_pts_ms", source)
        self.assertIn("playback_anchor->video_rtp_timestamp", source)
        self.assertIn("playback_anchor_mutex_", source)
        self.assertIn("decode_playout_report", source)
        self.assertIn("playout_report_tracker_.accept", source)
        self.assertIn("SyncController{}.decide", source)
        self.assertIn("viewerRenderedPositionMs", qml)
        self.assertIn("hostViewerDeltaMs", qml)
        self.assertIn("hostSyncAction", qml)
        self.assertIn("viewerRenderedAvailable", qml)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--qml", type=Path, required=True)
    parser.add_argument("--controller-source", type=Path, required=True)
    parser.add_argument("--controller-header", type=Path, required=True)
    parser.add_argument("--peer-source", type=Path, required=True)
    parser.add_argument("--movie-supported", action="store_true")
    args, unittest_args = parser.parse_known_args()
    RtcDemoCliTest.demo = args.demo.resolve()
    RtcDemoCliTest.qml = args.qml.resolve()
    RtcDemoCliTest.controller_source = args.controller_source.resolve()
    RtcDemoCliTest.controller_header = args.controller_header.resolve()
    RtcDemoCliTest.peer_source = args.peer_source.resolve()
    RtcDemoCliTest.movie_supported = args.movie_supported
    unittest.main(argv=[sys.argv[0], *unittest_args])
    return 0


if __name__ == "__main__":
    sys.exit(main())
