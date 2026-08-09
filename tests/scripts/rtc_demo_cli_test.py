#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
import time
import unittest
from pathlib import Path


class RtcDemoCliTest(unittest.TestCase):
    demo = Path()
    qml = Path()
    controller_source = Path()
    controller_header = Path()
    qt_audio_source = Path()
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

    def qml_source(self) -> str:
        if self.qml.is_dir():
            return "\n".join(
                path.read_text(encoding="utf-8")
                for path in sorted(self.qml.glob("*.qml"))
                if path.name != "LegacyCallView.qml"
            )
        return self.qml.read_text(encoding="utf-8")

    def qml_file(self, name: str) -> str:
        base = self.qml if self.qml.is_dir() else self.qml.parent
        return (base / name).read_text(encoding="utf-8")

    def test_help_documents_sender_receiver_contract(self):
        result = self.run_demo("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("--server", result.stdout)
        self.assertIn("--role", result.stdout)
        self.assertIn("--room", result.stdout)
        self.assertIn("--source", result.stdout)
        self.assertIn("test, desktop, movie, or screen", result.stdout)
        self.assertIn("--screen-profile", result.stdout)
        self.assertIn("--audio", result.stdout)
        self.assertIn("--no-audio-playout", result.stdout)
        self.assertIn("host or viewer", result.stdout)
        self.assertIn("--movie", result.stdout)
        self.assertIn("--movie-audio", result.stdout)
        self.assertIn("--video-acceleration", result.stdout)
        self.assertIn("--metrics-jsonl", result.stdout)
        self.assertIn("--drift-scenario", result.stdout)
        self.assertIn("--measurement-duration-seconds", result.stdout)

    def test_no_arguments_launches_interactive_home(self):
        environment = os.environ.copy()
        environment.setdefault("QT_QPA_PLATFORM", "offscreen")
        process = subprocess.Popen(
            [str(self.demo)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=environment,
        )
        try:
            time.sleep(0.5)
            self.assertIsNone(process.poll())
        finally:
            process.terminate()
            process.wait(timeout=5)

    def test_partial_or_invalid_explicit_options_are_usage_errors(self):
        self.assertEqual(
            self.run_demo("--server", "ws://127.0.0.1:18080/v1/ws").returncode,
            2,
        )
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

    def test_screen_source_and_profile_contract_on_supported_desktops(self):
        if sys.platform not in ("darwin", "win32"):
            self.skipTest("screen source contract requires macOS or Windows")

        host = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "screen", "--screen-profile", "quality", "--validate"
        )
        self.assertEqual(host.returncode, 0)
        viewer = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "viewer",
            "--room", "ABC234", "--source", "screen", "--validate"
        )
        self.assertEqual(viewer.returncode, 0)
        invalid_profile = self.run_demo(
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "test", "--screen-profile", "quality", "--validate"
        )
        self.assertEqual(invalid_profile.returncode, 2)

    def test_primary_voice_mode_and_playout_contract(self):
        if sys.platform != "darwin":
            self.skipTest("screen voice contract is macOS-specific")
        common = [
            "--server", "ws://127.0.0.1:18080/v1/ws", "--role", "host",
            "--source", "screen", "--validate",
        ]
        self.assertEqual(self.run_demo(*common).returncode, 0)
        self.assertEqual(
            self.run_demo(*common, "--audio", "microphone").returncode, 0
        )
        self.assertEqual(
            self.run_demo(
                *common, "--audio", "synthetic", "--no-audio-playout"
            ).returncode,
            0,
        )
        self.assertEqual(
            self.run_demo(*common, "--audio", "invalid").returncode, 2
        )
        cli_source = (self.controller_source.parent / "main.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'QStringLiteral("mode"), QStringLiteral("microphone")',
            cli_source,
        )
        self.assertIn("SignaledAudioMode::microphone", cli_source)
        self.assertIn("native_audio_playout", self.peer_source.read_text(
            encoding="utf-8"
        ))

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
        source = self.qml_file("CallDetailsDrawer.qml")
        self.assertIn("hostControlsAvailable", source)
        self.assertIn("pauseHostPlayback()", source)
        self.assertIn("resumeHostPlayback()", source)
        self.assertIn("seekHostPlayback(", source)
        self.assertIn("to: Math.max(0, drawer.controller.hostPlaybackDurationMs)", source)
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
        self.assertIn("SetAudioPlayout(config_.native_audio_playout)", peer)
        self.assertIn("native_audio_playout_", source)

    def test_controller_keeps_video_correction_observational(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("MovieVideoPlayoutSchedulerConfig::observational()", source)
        self.assertIn("activation.status", source)
        self.assertIn("movie_audio_output_ready_ = false", source)
        self.assertIn("movie-audio-output-activation-failed", source)
        self.assertIn("startMovieAudioViewerPath", source)
        self.assertIn("markAudioRouteTransition", source)
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

    def test_final_movie_audio_review_contracts(self):
        controller = self.controller_source.read_text(encoding="utf-8")
        qt_audio = self.qt_audio_source.read_text(encoding="utf-8")
        renderer = (self.controller_source.parent.parent.parent / "core" /
                    "src" / "movie_audio_renderer.cpp").read_text(
                        encoding="utf-8"
                    )

        audio_start = controller.index("if (const auto audio_clock =")
        audio_end = controller.index("\n      return;", audio_start)
        audio_handler = controller[audio_start:audio_end]
        self.assertIn("logical_consumed_frames", audio_handler)
        self.assertIn(".consumed_frames = audio_snapshot.logical_consumed_frames",
                      audio_handler)
        self.assertIn("pause_output", controller)
        self.assertIn("resume_output", controller)
        self.assertIn(".playing = remote_playback_state_ ==",
                      controller)
        self.assertIn("output_->stop()", renderer)
        self.assertIn("output_->open(config_.output_format)", renderer)
        self.assertIn("output_->start()", renderer)
        self.assertNotIn("AudioOutputDevice::flush", renderer)
        self.assertIn("state == QAudio::ActiveState", qt_audio)
        self.assertIn("state == QAudio::IdleState", qt_audio)

    def test_controller_teardown_is_dependency_safe(self):
        source = self.controller_source.read_text(encoding="utf-8")
        start = source.index("void RtcDemoController::stopPeer()")
        end = source.index("\nvoid RtcDemoController::flushDriftMetrics", start)
        teardown = source[start:end]
        ordered_markers = [
            "shutting_down_ = true",
            "audio_route_monitor_->stop",
            "audio_route_controller_.shutdown",
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

    def test_controller_starts_route_monitor_after_renderer_activation(self):
        source = self.controller_source.read_text(encoding="utf-8")
        start_method = source.index("void RtcDemoController::start()")
        start_peer = source.index("void RtcDemoController::startPeer()")
        self.assertNotIn(
            "audio_route_monitor_->start",
            source[start_method:start_peer],
        )

        stop_peer = source.index("void RtcDemoController::stopPeer()", start_peer)
        activation = source.index("movie_audio_renderer_->activate_output", start_peer)
        self.assertLess(activation, stop_peer)
        monitor_start = source.index("startAudioRouteMonitor();", activation)
        self.assertLess(activation, monitor_start)
        monitor_helper = source.index("void RtcDemoController::startAudioRouteMonitor")
        self.assertIn("audio_route_monitor_->start", source[monitor_helper:activation])
        self.assertIn("audio_route_controller_.on_route_notification", source)
        self.assertIn("movie_audio_renderer_->activate_output", source)
        self.assertIn("complete_candidate_activation", source)
        self.assertIn("audio_route_transition_pending_", source)
        pump_start = source.index("void RtcDemoController::pumpMovieAudio()")
        pump_end = source.index("\nvoid RtcDemoController::recordRenderedFrame", pump_start)
        pump = source[pump_start:pump_end]
        self.assertIn("audio_route_transition_pending_ = false", pump)
        self.assertIn(".route_transition = route_transition", pump)

    def test_route_replacement_preserves_independent_peer_paths(self):
        source = self.controller_source.read_text(encoding="utf-8")
        start = source.index("void RtcDemoController::handleAudioRouteEvent")
        end = source.index("\nvoid RtcDemoController::startPeer()", start)
        route_handler = source[start:end]
        self.assertIn("AudioRouteActivationResult", route_handler)
        self.assertIn("complete_candidate_activation", route_handler)
        self.assertNotIn("movie_peer_->stop", route_handler)
        self.assertNotIn("peer_->stop", route_handler)
        self.assertNotIn("movie_peer_.reset", route_handler)
        self.assertNotIn("peer_.reset", route_handler)

    def test_route_transition_blocks_scheduler_before_remote_video_submission(self):
        source = self.controller_source.read_text(encoding="utf-8")
        remote_sink = (self.controller_source.parent.parent.parent / "rtc" /
                       "webrtc" / "src" / "remote_video_sink.hpp").read_text(
                           encoding="utf-8"
                       )
        route_start = source.index("void RtcDemoController::handleAudioRouteEvent")
        route_end = source.index("\nvoid RtcDemoController::startPeer()", route_start)
        route_handler = source[route_start:route_end]
        self.assertLess(
            route_handler.index("markAudioRouteTransition()"),
            route_handler.index("movie_audio_renderer_->activate_output"),
        )
        transition_start = source.index(
            "void RtcDemoController::markAudioRouteTransition"
        )
        transition_end = source.index(
            "\nvoid RtcDemoController::handleAudioRouteEvent", transition_start
        )
        transition = source[transition_start:transition_end]
        self.assertIn("movie_video_playout_adapter_->advance", transition)
        self.assertIn("ClockConfidence::invalid", transition)
        self.assertIn(".route_transition = true", transition)
        deliver_start = source.index("void RtcDemoController::deliverRemoteFrame")
        deliver_end = source.index("\nvoid RtcDemoController::pumpMovieAudio", deliver_start)
        self.assertIn("movie_video_playout_adapter_->submit", source[deliver_start:deliver_end])
        self.assertIn("callback_(frame)", remote_sink)

    def test_initial_route_observation_is_adopted_without_renderer_handoff(self):
        source = self.controller_source.read_text(encoding="utf-8")
        header = self.controller_header.read_text(encoding="utf-8")
        self.assertIn("audio_route_monitor_initial_observation_pending_", header)
        monitor_start = source.index("void RtcDemoController::startAudioRouteMonitor")
        monitor_end = source.index("\nvoid RtcDemoController::handleAudioRouteEvent", monitor_start)
        monitor = source[monitor_start:monitor_end]
        self.assertIn(
            "audio_route_monitor_initial_observation_pending_ =\n      movie_audio_output_ready_",
            monitor,
        )
        handler_start = source.index("void RtcDemoController::handleAudioRouteEvent")
        initial_start = source.index(
            "if (audio_route_monitor_initial_observation_pending_)", handler_start
        )
        activation = source.index("movie_audio_renderer_->activate_output", initial_start)
        initial = source[initial_start:activation]
        self.assertIn("audio_route_controller_.on_route_notification", initial)
        self.assertIn("complete_candidate_activation", initial)
        self.assertIn("return;", initial)

    def test_failed_initial_output_keeps_route_recovery_and_peer_startable(self):
        source = self.controller_source.read_text(encoding="utf-8")
        header = self.controller_header.read_text(encoding="utf-8")
        self.assertIn("movie_audio_peer_started_", header)
        handler_start = source.index("void RtcDemoController::handleAudioRouteEvent")
        handler_end = source.index("\nvoid RtcDemoController::startPeer()", handler_start)
        handler = source[handler_start:handler_end]
        self.assertNotIn("!movie_audio_output_ready_", handler[:handler.index("const auto notification")])
        self.assertIn("startMovieAudioViewerPath()", handler)
        self.assertIn("|| movie_audio_peer_started_)", source)
        peer_start = source.index("void RtcDemoController::startMovieAudioPeer")
        peer_end = source.index("\nvoid RtcDemoController::startMovieAudioViewerPath", peer_start)
        self.assertIn("movie_peer_->start()", source[peer_start:peer_end])
        self.assertIn("movie_waiter_ = std::jthread", source[peer_start:peer_end])

    def test_route_attempt_restores_paused_output_state(self):
        source = self.controller_source.read_text(encoding="utf-8")
        start = source.index("void RtcDemoController::handleAudioRouteEvent")
        end = source.index("\nvoid RtcDemoController::startPeer()", start)
        handler = source[start:end]
        self.assertIn("const bool playback_paused", handler)
        activation = handler.index("movie_audio_renderer_->activate_output")
        snapshot = handler.index("const auto renderer_snapshot", activation)
        pause = handler.index("movie_audio_renderer_->pause_output()", snapshot)
        complete = handler.index("complete_candidate_activation", pause)
        self.assertLess(activation, snapshot)
        self.assertLess(snapshot, pause)
        self.assertLess(pause, complete)

    def test_route_event_activates_the_exact_resolved_qt_device(self):
        controller = self.controller_source.read_text(encoding="utf-8")
        route = (self.controller_source.parent / "qt_audio_route_monitor.cpp").read_text(
            encoding="utf-8"
        )
        handler_start = controller.index("void RtcDemoController::handleAudioRouteEvent")
        handler_end = controller.index("\nvoid RtcDemoController::startPeer()", handler_start)
        handler = controller[handler_start:handler_end]
        self.assertIn("resolve_device_for_event", route)
        self.assertIn("if (!candidate_device)", handler)
        self.assertLess(
            handler.index("resolve_device_for_event"),
            handler.index("audio_route_controller_.on_route_notification"),
        )
        self.assertIn("std::make_unique<QtAudioOutputDevice>(*candidate_device)", handler)
        self.assertNotIn(
            "std::make_unique<QtAudioOutputDevice>()",
            handler,
        )
        self.assertIn("QAudioDevice{}", route)

    def test_qt_output_preserves_pause_state_and_handles_idle_fail_closed(self):
        source = self.qt_audio_source.read_text(encoding="utf-8")
        pause_start = source.index("void QtAudioOutputDevice::pause()")
        pause_end = source.index("\nvoid QtAudioOutputDevice::stop()", pause_start)
        pause = source[pause_start:pause_end]
        quiesce_start = source.index(
            "QtAudioOutputDevice::quiesce_and_snapshot()"
        )
        quiesce_end = source.index("\nvoid QtAudioOutputDevice::pause()", quiesce_start)
        quiesce = source[quiesce_start:quiesce_end]
        self.assertNotIn("controlled_suspension_ = false", pause)
        self.assertIn("controlled_suspension_pending_", quiesce)
        self.assertIn("is_writable_sink_state", quiesce)
        self.assertIn("exact_consumption = quiesced", quiesce)

    def test_native_route_status_and_pulse_callbacks_are_observable_and_contained(self):
        route_header = (self.controller_source.parent / "qt_audio_route_monitor.hpp").read_text(
            encoding="utf-8"
        )
        route = (self.controller_source.parent / "qt_audio_route_monitor.cpp").read_text(
            encoding="utf-8"
        )
        controller = self.controller_source.read_text(encoding="utf-8")
        linux = (self.controller_source.parent / "linux_audio_route_monitor.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("native_supplement_status", route_header)
        self.assertIn("AudioRouteNativeSupplementStatus::start_failed", route)
        self.assertIn("native_supplement_status()", controller)
        self.assertIn("audioRouteMonitorStatus", self.controller_header.read_text(encoding="utf-8"))
        self.assertIn("audioRouteMonitorStatus", self.qml_source())

        pulse_server = linux[linux.index("static void server_info_ready"):linux.index("static void subscription_changed")]
        subscription_start = linux.index("static void subscription_changed")
        pulse_subscription = linux[subscription_start:linux.index("Callback callback_", subscription_start)]
        self.assertIn("monitor->notify(", pulse_server)
        self.assertIn("monitor->notify(", pulse_subscription)
        self.assertNotIn("monitor->callback_", pulse_server)
        self.assertNotIn("monitor->callback_", pulse_subscription)
        notify_start = linux.index("void notify(", subscription_start)
        notify_end = linux.index("Callback callback_", notify_start)
        notify = linux[notify_start:notify_end]
        self.assertIn("try", notify)
        self.assertIn("catch (...)", notify)

    def test_controller_exposes_route_renderer_and_scheduler_diagnostics(self):
        source = self.controller_source.read_text(encoding="utf-8")
        header = self.controller_header.read_text(encoding="utf-8")
        qml = self.qml_source()
        for property_name in (
            "audioRouteGeneration",
            "audioRendererQueueDurationMs",
            "audioDeviceQueueDurationMs",
            "audioUnderrunCount",
            "audioLastDiscontinuityCategory",
        ):
            self.assertIn(property_name, header)
            self.assertIn(property_name, qml)
        for field in (
            "route_generation",
            "clock_confidence",
            "renderer_queue_duration",
            "device_queue_duration",
            "underrun_count",
            "last_discontinuity_reason",
        ):
            self.assertIn(field, source)
        self.assertIn("playback_category_name", source)
        self.assertIn("viewerSuggestedAction", qml)
        self.assertIn("viewerAppliedAction", qml)

    def test_route_diagnostics_do_not_expose_identifiers_or_wire_fields(self):
        source = self.controller_source.read_text(encoding="utf-8")
        qml = self.qml_file("CallDetailsDrawer.qml")
        start = source.index("void RtcDemoController::refreshAudioDiagnostics")
        end = source.index("\nvoid RtcDemoController::publishPlayoutReport", start)
        diagnostics = source[start:end]
        for forbidden in (
            "stable_device_id",
            "device_instance_id",
            "room_id_",
            "sdp",
            "candidate",
            "movie_path_",
        ):
            self.assertNotIn(forbidden, diagnostics)
        qml_diagnostics = qml[qml.index("id: audioDiagnostics") :]
        self.assertNotIn("roomId", qml_diagnostics)

    def test_controller_routes_host_local_and_viewer_remote_video(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("config.local_video_frame", source)
        self.assertIn("if (role_ == shareme::rtc::SignaledRole::host)", source)
        self.assertIn("config.remote_video_frame", source)
        self.assertIn("if (role_ == shareme::rtc::SignaledRole::viewer)", source)

    def test_screen_encoder_and_capture_are_host_only(self):
        source = self.controller_source.read_text(encoding="utf-8")
        create_peer = source.index("bool RtcDemoController::createPeer()")
        start = source.index("if (screen_source_", create_peer)
        end = source.index("#if defined(SHAREME_HAS_MOVIE_RTC)", start)
        screen_setup = source[start:end]
        self.assertIn("role_ == shareme::rtc::SignaledRole::host", screen_setup)
        self.assertIn("selection.capture_profile", screen_setup)

    def test_screen_capture_uses_a_valid_bounded_stream_queue(self):
        capture_source = (
            self.controller_source.parent.parent.parent
            / "rtc" / "screen" / "src" / "macos_screen_capture_source.mm"
        ).read_text(encoding="utf-8")
        self.assertIn("configuration.queueDepth = 3", capture_source)
        self.assertNotIn("configuration.queueDepth = 2", capture_source)

    def test_receiver_waiting_overlay_tracks_submitted_video(self):
        controller = self.controller_source.read_text(encoding="utf-8")
        header = self.controller_header.read_text(encoding="utf-8")
        qml = self.qml_file("VideoStage.qml")
        self.assertIn("remoteVideoAvailable", header)
        self.assertIn("remote_video_available_", controller)
        self.assertIn("remoteVideoAvailableChanged", controller)
        overlay = qml[qml.index("Column {"):]
        self.assertIn("remoteVideoAvailable", overlay)
        self.assertIn("stage.controller.viewer", overlay)

    def test_controller_records_remote_dimensions_for_performance_counters(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("performance_frame_width_.store", source)
        self.assertIn("performance_frame_height_.store", source)

    def test_controller_monitors_late_screen_capture_failures(self):
        source = self.controller_source.read_text(encoding="utf-8")
        header = self.controller_header.read_text(encoding="utf-8")
        peer = self.peer_source.read_text(encoding="utf-8")
        self.assertIn("screen_capture_error_timer_", header)
        self.assertIn("checkScreenCaptureError", source)
        self.assertIn("video_source_error", peer)
        self.assertIn('screen-capture-error:', source)

    def test_controller_exposes_screen_encoder_and_presentation_diagnostics(self):
        source = self.controller_source.read_text(encoding="utf-8")
        header = self.controller_header.read_text(encoding="utf-8")
        qml = self.qml_source()
        for property_name in (
            "videoSource",
            "screenProfile",
            "videoCaptureProfile",
            "videoEncoderImplementation",
            "videoNegotiatedCodec",
            "videoHardwareStatus",
            "presentationCallbacks",
            "presentationSubmissions",
            "presentationCoalesced",
            "presentationDelayP95Ms",
            "presentationDelayMaxMs",
        ):
            self.assertIn(property_name, header)
            self.assertIn(property_name, qml)
        self.assertIn("video_encoder_diagnostics_", source)
        self.assertIn("presentation_callback_delay_p95", source)
        self.assertIn("videoHardwareStatus", source)
        self.assertIn("webrtc_encoder", source)
        self.assertIn("encoder_implementation", source)
        self.assertIn("bitrate_bps", source)
        self.assertIn("videoEncoderImplementation()", source)
        self.assertIn('return QStringLiteral("receive-only")', source)
        self.assertIn('return QStringLiteral("remote-unreported")', source)

    def test_performance_stats_do_not_block_counter_timer(self):
        source = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("performance_stats_worker_", source)
        self.assertIn("performance_stats_mutex_", source)
        start = source.index("void RtcDemoController::emitPerformanceCounters()")
        end = source.index("\nvoid RtcDemoController::stopDriftMetrics()", start)
        self.assertNotIn("peer_->media_stats()", source[start:end])

    def test_control_message_send_does_not_block_the_qt_thread(self):
        source = self.peer_source.read_text(encoding="utf-8")
        controller = self.controller_source.read_text(encoding="utf-8")
        self.assertIn("queue_control_message(", controller)
        self.assertIn("bool queue_control_message(", source)
        self.assertIn("std::function<void(bool)>", source)
        start = source.index("bool queue_control_message(")
        end = source.index("SignaledMediaStats media_stats()", start)
        method = source[start:end]
        self.assertIn("PostTask", method)
        self.assertNotIn("BlockingCall", method)

    def test_media_stats_poll_does_not_block_on_stats_schedule(self):
        source = self.peer_source.read_text(encoding="utf-8")
        start = source.index("SignaledMediaStats media_stats()")
        end = source.index("SignaledPeerResult wait", start)
        method = source[start:end]
        self.assertIn("PostTask", method)
        self.assertNotIn("BlockingCall", method)

    def test_primary_voice_playout_and_counters_are_wired(self):
        peer = self.peer_source.read_text(encoding="utf-8")
        controller = self.controller_source.read_text(encoding="utf-8")
        for counter in (
            "voice_packets_sent",
            "voice_packets_received",
            "voice_bytes_sent",
            "voice_bytes_received",
        ):
            self.assertIn(counter, peer)
            self.assertIn(counter, controller)

    def test_presentation_recovery_probe_is_screen_only(self):
        controller = self.controller_source.read_text(encoding="utf-8")
        self.assertIn(
            "viewer() && screen_source_ && screen_recovery_probe_enabled_",
            controller,
        )

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
        qml = self.qml_source()
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
    parser.add_argument("--qt-audio-source", type=Path, required=True)
    parser.add_argument("--peer-source", type=Path, required=True)
    parser.add_argument("--movie-supported", action="store_true")
    args, unittest_args = parser.parse_known_args()
    RtcDemoCliTest.demo = args.demo.resolve()
    RtcDemoCliTest.qml = args.qml.resolve()
    RtcDemoCliTest.controller_source = args.controller_source.resolve()
    RtcDemoCliTest.controller_header = args.controller_header.resolve()
    RtcDemoCliTest.qt_audio_source = args.qt_audio_source.resolve()
    RtcDemoCliTest.peer_source = args.peer_source.resolve()
    RtcDemoCliTest.movie_supported = args.movie_supported
    unittest.main(argv=[sys.argv[0], *unittest_args])
    return 0


if __name__ == "__main__":
    sys.exit(main())
