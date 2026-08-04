#include "api/ref_counted_base.h"
#include "api/video/i420_buffer.h"
#include "counting_audio_sink.hpp"
#include "remote_video_sink.hpp"
#include "shareme/rtc/local_audio_source.hpp"
#include "shareme/rtc/local_video_source.hpp"
#include "shareme/rtc/signaled_peer.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>
namespace {
void require(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "Requirement failed at line " << line << ": " << expression
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

template <typename T>
concept HasMovieAudioFactory = requires { &T::movie_audio_source_factory; };

static_assert(!HasMovieAudioFactory<shareme::rtc::SignaledPeerConfig>);
} // namespace
#define REQUIRE(expression) require((expression), #expression, __LINE__)
int main() {
  using shareme::rtc::SignaledAudioMode;
  using shareme::rtc::SignaledRole;
  using shareme::rtc::SignaledVideoMode;
  int remote_callback_count = 0;
  int remote_width = 0;
  std::int64_t remote_timestamp_us = 0;
  shareme::rtc::RemoteVideoSink remote_sink(
      [&](const webrtc::VideoFrame &frame) {
        ++remote_callback_count;
        remote_width = frame.width();
        remote_timestamp_us = frame.timestamp_us();
      });
  auto remote_buffer = webrtc::I420Buffer::Create(4, 2);
  remote_buffer->InitializeData();
  auto remote_frame =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(remote_buffer)
          .set_timestamp_us(123'000)
          .set_rtp_timestamp(456)
          .build();
  remote_sink.OnFrame(remote_frame);
  REQUIRE(remote_sink.frame_count() == 1);
  REQUIRE(remote_sink.last_width() == 4);
  REQUIRE(remote_sink.last_height() == 2);
  REQUIRE(remote_callback_count == 1);
  REQUIRE(remote_width == 4);
  REQUIRE(remote_timestamp_us == 123'000);
  remote_sink.clear_callback();
  remote_sink.OnFrame(remote_frame);
  REQUIRE(remote_sink.frame_count() == 2);
  REQUIRE(remote_callback_count == 1);
  const auto synthetic_policy =
      shareme::rtc::signaled_audio_policy(SignaledAudioMode::synthetic);
  REQUIRE(!synthetic_policy.uses_native_microphone);
  REQUIRE(!synthetic_policy.processing_enabled);
  const auto microphone_policy =
      shareme::rtc::signaled_audio_policy(SignaledAudioMode::microphone);
  REQUIRE(microphone_policy.uses_native_microphone);
  REQUIRE(microphone_policy.processing_enabled);
  REQUIRE(shareme::rtc::valid_signaled_peer_config(
      {.role = SignaledRole::host,
       .audio_mode = SignaledAudioMode::microphone}));
  REQUIRE(!shareme::rtc::valid_signaled_peer_config(
      {.role = SignaledRole::host,
       .audio_mode = SignaledAudioMode::synthetic,
       .video_mode = SignaledVideoMode::injected}));
  shareme::rtc::CountingAudioSink audio_sink;
  std::vector<std::int16_t> samples(480 * 2);
  samples[0] = -12;
  samples[1] = 31'000;
  samples[2] = -32'768;
  samples[3] = 2;
  audio_sink.OnData(samples.data(), 16, 48'000, 2, 480, 1);
  REQUIRE(audio_sink.valid_callback_count() == 1);
  REQUIRE(audio_sink.invalid_callback_count() == 0);
  REQUIRE(audio_sink.sample_rate() == 48'000);
  REQUIRE(audio_sink.channels() == 2);
  REQUIRE(audio_sink.peak() == 32'768);
  const auto first_audio_snapshot = audio_sink.snapshot();
  REQUIRE(first_audio_snapshot.valid_callback_count == 1);
  REQUIRE(first_audio_snapshot.invalid_callback_count == 0);
  REQUIRE(first_audio_snapshot.sample_rate == 48'000);
  REQUIRE(first_audio_snapshot.channels == 2);
  REQUIRE(first_audio_snapshot.peak == 32'768);
  audio_sink.OnData(nullptr, 16, 48'000, 2, 480, 2);
  audio_sink.OnData(samples.data(), 32, 48'000, 2, 480, 3);
  audio_sink.OnData(samples.data(), 16, 48'000, 2, 479, 4);
  audio_sink.OnData(samples.data(), 16, 48'000, 1, 480, 5);
  REQUIRE(audio_sink.valid_callback_count() == 1);
  REQUIRE(audio_sink.invalid_callback_count() == 4);
  auto publish_second_callback = std::async(std::launch::async, [&audio_sink] {
    std::vector<std::int16_t> quiet_samples(480 * 2);
    quiet_samples[0] = -1;
    quiet_samples[1] = 2;
    audio_sink.OnData(quiet_samples.data(), 16, 48'000, 2, 480, 6);
  });
  publish_second_callback.get();
  const auto second_audio_snapshot = audio_sink.snapshot();
  REQUIRE(second_audio_snapshot.valid_callback_count == 2);
  REQUIRE(second_audio_snapshot.invalid_callback_count == 4);
  REQUIRE(second_audio_snapshot.sample_rate == 48'000);
  REQUIRE(second_audio_snapshot.channels == 2);
  REQUIRE(second_audio_snapshot.peak == 32'768);
  REQUIRE(
      shareme::rtc::has_sufficient_movie_audio_reception(false, false, 0));
  REQUIRE(
      !shareme::rtc::has_sufficient_movie_audio_reception(true, false, 100));
  REQUIRE(
      !shareme::rtc::has_sufficient_movie_audio_reception(true, true, 99));
  REQUIRE(
      shareme::rtc::has_sufficient_movie_audio_reception(true, true, 100));
  REQUIRE(shareme::rtc::is_expected_voice_rtp_track(
      SignaledRole::host, true, "host-voice"));
  REQUIRE(shareme::rtc::is_expected_voice_rtp_track(
      SignaledRole::host, false, "viewer-voice"));
  REQUIRE(shareme::rtc::is_expected_voice_rtp_track(
      SignaledRole::viewer, true, "viewer-voice"));
  REQUIRE(shareme::rtc::is_expected_voice_rtp_track(
      SignaledRole::viewer, false, "host-voice"));
  REQUIRE(!shareme::rtc::is_expected_voice_rtp_track(
      SignaledRole::host, true, "movie-audio"));
  REQUIRE(!shareme::rtc::is_expected_voice_rtp_track(
      SignaledRole::viewer, false, "movie-audio"));
  REQUIRE(!shareme::rtc::is_expected_voice_rtp_track(
      SignaledRole::viewer, true, ""));
  REQUIRE(shareme::rtc::is_expected_inbound_voice_rtp_track(
      SignaledRole::viewer, "receiver-uuid", "voice-mid", "voice-mid"));
  REQUIRE(!shareme::rtc::is_expected_inbound_voice_rtp_track(
      SignaledRole::viewer, "receiver-uuid", "movie-mid", "voice-mid"));
  REQUIRE(!shareme::rtc::is_expected_inbound_voice_rtp_track(
      SignaledRole::viewer, "movie-audio", "movie-mid", "voice-mid"));
  std::string invalid_video_error;
  shareme::rtc::SignaledPeerCallbacks invalid_video_callbacks;
  invalid_video_callbacks.failure = [&](std::string category) {
    invalid_video_error = std::move(category);
  };
  auto invalid_video_peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host,
       .audio_mode = SignaledAudioMode::synthetic,
       .video_mode = SignaledVideoMode::injected},
      std::move(invalid_video_callbacks));
  REQUIRE(invalid_video_peer == nullptr);
  REQUIRE(invalid_video_error == "invalid-video-mode");
  shareme::rtc::LocalVideoSourceFactory empty_video_factory =
      [](webrtc::TaskQueueFactory &) {
        return webrtc::scoped_refptr<shareme::rtc::LocalVideoSource>{};
      };
  REQUIRE(shareme::rtc::valid_signaled_peer_config(
      {.role = SignaledRole::host,
       .audio_mode = SignaledAudioMode::synthetic,
       .video_mode = SignaledVideoMode::injected,
       .video_source_factory = empty_video_factory}));
  std::string video_error;
  shareme::rtc::SignaledPeerCallbacks video_callbacks;
  video_callbacks.failure = [&](std::string category) {
    video_error = std::move(category);
  };
  auto missing_video_peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host,
       .audio_mode = SignaledAudioMode::synthetic,
       .video_mode = SignaledVideoMode::injected,
       .video_source_factory = std::move(empty_video_factory)},
      std::move(video_callbacks));
  REQUIRE(missing_video_peer == nullptr);
  REQUIRE(video_error == "video-source-unavailable");
  const auto invalid_mode = static_cast<SignaledAudioMode>(99);
  REQUIRE(!shareme::rtc::valid_signaled_peer_config(
      {.role = SignaledRole::host, .audio_mode = invalid_mode}));
  std::string config_error;
  shareme::rtc::SignaledPeerCallbacks invalid_callbacks;
  invalid_callbacks.failure = [&](std::string category) {
    config_error = std::move(category);
  };
  auto invalid_peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host, .audio_mode = invalid_mode},
      std::move(invalid_callbacks));
  REQUIRE(invalid_peer == nullptr);
  REQUIRE(config_error == "invalid-audio-mode");
  config_error.clear();
  shareme::rtc::SignaledPeerCallbacks invalid_role_callbacks;
  invalid_role_callbacks.failure = [&](std::string category) {
    config_error = std::move(category);
  };
  invalid_peer = shareme::rtc::SignaledPeer::create(
      {.role = static_cast<SignaledRole>(99),
       .audio_mode = SignaledAudioMode::synthetic},
      std::move(invalid_role_callbacks));
  REQUIRE(invalid_peer == nullptr);
  REQUIRE(config_error == "invalid-role");
  REQUIRE(shareme::rtc::valid_remote_description(SignaledRole::viewer, "offer",
                                                 "v=0\r\n"));
  REQUIRE(shareme::rtc::valid_remote_description(SignaledRole::host, "answer",
                                                 "v=0\r\n"));
  REQUIRE(!shareme::rtc::valid_remote_description(SignaledRole::host, "offer",
                                                  "v=0\r\n"));
  REQUIRE(!shareme::rtc::valid_remote_description(SignaledRole::viewer,
                                                  "answer", ""));
  REQUIRE(shareme::rtc::valid_remote_candidate(
      "0", 0, "candidate:1 1 udp 1 127.0.0.1 9 typ host"));
  REQUIRE(!shareme::rtc::valid_remote_candidate("", -1, ""));
  REQUIRE(shareme::rtc::valid_control_message("{\"type\":\"playback-state\"}"));
  REQUIRE(!shareme::rtc::valid_control_message(""));
  REQUIRE(!shareme::rtc::valid_control_message(std::string(64 * 1024 + 1, 'x')));
  REQUIRE(shareme::rtc::valid_control_channel("shareme-control-v1", true,
                                              true));
  REQUIRE(!shareme::rtc::valid_control_channel("shareme-control-v1", true,
                                               false));
  REQUIRE(!shareme::rtc::valid_control_channel("shareme-control-v1", false,
                                               true));
  REQUIRE(!shareme::rtc::valid_control_channel("other", true, true));

  std::unique_ptr<shareme::rtc::SignaledPeer> control_host;
  std::unique_ptr<shareme::rtc::SignaledPeer> control_viewer;
  std::promise<std::string> control_message_promise;
  auto control_message_future = control_message_promise.get_future();
  std::promise<std::string> viewer_report_promise;
  auto viewer_report_future = viewer_report_promise.get_future();
  shareme::rtc::SignaledPeerCallbacks host_control_callbacks;
  host_control_callbacks.description = [&](std::string type, std::string sdp) {
    REQUIRE(control_viewer->receive_description(std::move(type),
                                                std::move(sdp)));
  };
  host_control_callbacks.candidate =
      [&](std::string mid, int line, std::string candidate) {
        REQUIRE(control_viewer->receive_candidate(
            std::move(mid), line, std::move(candidate)));
      };
  shareme::rtc::SignaledPeerCallbacks viewer_control_callbacks;
  viewer_control_callbacks.description =
      [&](std::string type, std::string sdp) {
        REQUIRE(control_host->receive_description(std::move(type),
                                                  std::move(sdp)));
      };
  viewer_control_callbacks.candidate =
      [&](std::string mid, int line, std::string candidate) {
        REQUIRE(control_host->receive_candidate(
            std::move(mid), line, std::move(candidate)));
      };
  control_viewer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::viewer,
       .control_message = [&](std::string message) {
         control_message_promise.set_value(std::move(message));
       }},
      std::move(viewer_control_callbacks));
  control_host = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host,
       .control_message = [&](std::string message) {
         viewer_report_promise.set_value(std::move(message));
       }},
      std::move(host_control_callbacks));
  REQUIRE(control_viewer != nullptr);
  REQUIRE(control_host != nullptr);
  REQUIRE(control_viewer->start());
  REQUIRE(control_host->start());
  const std::string control_payload{"{\"type\":\"playback-state\"}"};
  bool control_sent = false;
  for (int attempt = 0; attempt < 250 && !control_sent; ++attempt) {
    control_sent = control_host->send_control_message(control_payload);
    if (!control_sent)
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  REQUIRE(control_sent);
  REQUIRE(control_message_future.wait_for(std::chrono::seconds(2)) ==
          std::future_status::ready);
  REQUIRE(control_message_future.get() == control_payload);
  const std::string viewer_report{"{\"type\":\"playout-report\"}"};
  REQUIRE(control_viewer->send_control_message(viewer_report));
  REQUIRE(viewer_report_future.wait_for(std::chrono::seconds(2)) ==
          std::future_status::ready);
  REQUIRE(viewer_report_future.get() == viewer_report);
  control_host->stop();
  control_viewer->stop();

  auto viewer_only = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::viewer}, {});
  REQUIRE(viewer_only != nullptr);
  REQUIRE(!viewer_only->send_control_message("{}"));
  viewer_only->stop();
  std::promise<std::pair<int, int>> local_preview_promise;
  auto local_preview_future = local_preview_promise.get_future();
  std::atomic_bool local_preview_reported{false};
  auto peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host,
       .audio_mode = SignaledAudioMode::synthetic,
       .local_video_frame = [&](const webrtc::VideoFrame &frame) {
         if (!local_preview_reported.exchange(true))
           local_preview_promise.set_value({frame.width(), frame.height()});
       }},
      {});
  REQUIRE(peer != nullptr);
  REQUIRE(!peer->send_control_message("{}"));
  REQUIRE(peer->start());
  REQUIRE(local_preview_future.wait_for(std::chrono::seconds(2)) ==
          std::future_status::ready);
  const auto local_preview_size = local_preview_future.get();
  REQUIRE(local_preview_size.first == 640);
  REQUIRE(local_preview_size.second == 360);
  auto wait_result = std::async(
      std::launch::async, [&] { return peer->wait(std::chrono::seconds(15)); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  peer->cancel_wait();
  REQUIRE(wait_result.wait_for(std::chrono::milliseconds(500)) ==
          std::future_status::ready);
  REQUIRE(wait_result.get().error == "signaled call cancelled");
  peer->stop();
  peer.reset();

}
