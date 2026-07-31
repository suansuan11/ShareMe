#include "shareme/rtc/local_video_source.hpp"
#include "shareme/rtc/signaled_peer.hpp"
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>
namespace {
void require(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "Requirement failed at line " << line << ": " << expression
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}
} // namespace
#define REQUIRE(expression) require((expression), #expression, __LINE__)
int main() {
  using shareme::rtc::SignaledAudioMode;
  using shareme::rtc::SignaledRole;
  using shareme::rtc::SignaledVideoMode;
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
  auto peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host, .audio_mode = SignaledAudioMode::synthetic},
      {});
  REQUIRE(peer != nullptr);
  REQUIRE(peer->start());
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
