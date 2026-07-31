#include "api/ref_counted_base.h"
#include "counting_audio_sink.hpp"
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
namespace {
void require(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "Requirement failed at line " << line << ": " << expression
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}

struct FakeAudioState {
  std::atomic_bool started{false};
  std::atomic_bool stopped{false};
  bool start_succeeds{true};
  std::string error;
};

class FakeMovieAudioSource final : private webrtc::RefCountedBase,
                                   public shareme::rtc::LocalAudioSource {
public:
  explicit FakeMovieAudioSource(std::shared_ptr<FakeAudioState> state)
      : state_(std::move(state)) {}

  bool start() override {
    state_->started.store(true);
    return state_->start_succeeds;
  }
  void stop() noexcept override { state_->stopped.store(true); }
  std::uint64_t generated_count() const noexcept override { return 123; }
  std::optional<std::int64_t> last_pts_ms() const noexcept override {
    return 1'000;
  }
  std::string error() const override { return state_->error; }
  void AddSink(webrtc::AudioTrackSinkInterface *) override {}
  void RemoveSink(webrtc::AudioTrackSinkInterface *) override {}
  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  ~FakeMovieAudioSource() override = default;
  std::shared_ptr<FakeAudioState> state_;
};
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
  shareme::rtc::LocalAudioSourceFactory movie_audio_factory = [] {
    return webrtc::scoped_refptr<shareme::rtc::LocalAudioSource>(
        new FakeMovieAudioSource(std::make_shared<FakeAudioState>()));
  };
  REQUIRE(shareme::rtc::valid_signaled_peer_config(
      {.role = SignaledRole::host,
       .movie_audio_source_factory = movie_audio_factory}));
  REQUIRE(!shareme::rtc::valid_signaled_peer_config(
      {.role = SignaledRole::viewer,
       .movie_audio_source_factory = movie_audio_factory}));
  std::string missing_movie_audio_error;
  shareme::rtc::SignaledPeerCallbacks missing_movie_audio_callbacks;
  missing_movie_audio_callbacks.failure = [&](std::string category) {
    missing_movie_audio_error = std::move(category);
  };
  auto missing_movie_audio_peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host,
       .movie_audio_source_factory =
           [] {
             return webrtc::scoped_refptr<shareme::rtc::LocalAudioSource>{};
           }},
      std::move(missing_movie_audio_callbacks));
  REQUIRE(missing_movie_audio_peer == nullptr);
  REQUIRE(missing_movie_audio_error == "movie-audio-source-unavailable");

  shareme::rtc::CountingAudioSink audio_sink;
  const std::int16_t samples[]{-12, 31'000, -32'768, 2};
  audio_sink.OnData(samples, 16, 48'000, 2, 2, 1);
  REQUIRE(audio_sink.callback_count() == 1);
  REQUIRE(audio_sink.sample_rate() == 48'000);
  REQUIRE(audio_sink.channels() == 2);
  REQUIRE(audio_sink.peak() == 32'768);
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

  auto fake_state = std::make_shared<FakeAudioState>();
  auto movie_peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host,
       .movie_audio_source_factory =
           [fake_state] {
             return webrtc::scoped_refptr<shareme::rtc::LocalAudioSource>(
                 new FakeMovieAudioSource(fake_state));
           }},
      {});
  REQUIRE(movie_peer != nullptr);
  REQUIRE(movie_peer->start());
  REQUIRE(fake_state->started.load());
  movie_peer->cancel_wait();
  const auto movie_result = movie_peer->wait(std::chrono::seconds(1));
  REQUIRE(movie_result.movie_audio_chunks_generated == 123);
  movie_peer->stop();
  REQUIRE(fake_state->stopped.load());
  movie_peer.reset();

  auto failing_state = std::make_shared<FakeAudioState>();
  failing_state->start_succeeds = false;
  failing_state->error = "/private/movie.mov: decoder exploded";
  std::string sanitized_start_error;
  shareme::rtc::SignaledPeerCallbacks failing_callbacks;
  failing_callbacks.failure = [&](std::string category) {
    sanitized_start_error = std::move(category);
  };
  auto failing_movie_peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host,
       .movie_audio_source_factory =
           [failing_state] {
             return webrtc::scoped_refptr<shareme::rtc::LocalAudioSource>(
                 new FakeMovieAudioSource(failing_state));
           }},
      std::move(failing_callbacks));
  REQUIRE(failing_movie_peer != nullptr);
  REQUIRE(!failing_movie_peer->start());
  REQUIRE(sanitized_start_error == "movie-audio-source-start-failed");
  REQUIRE(sanitized_start_error.find("movie.mov") == std::string::npos);
  failing_movie_peer->stop();
}
