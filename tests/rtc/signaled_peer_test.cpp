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
  std::atomic<int> stop_count{0};
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
  void stop() noexcept override { state_->stop_count.fetch_add(1); }
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
  const auto first_audio_snapshot = audio_sink.snapshot();
  REQUIRE(first_audio_snapshot.callback_count == 1);
  REQUIRE(first_audio_snapshot.sample_rate == 48'000);
  REQUIRE(first_audio_snapshot.channels == 2);
  REQUIRE(first_audio_snapshot.peak == 32'768);
  auto publish_second_callback = std::async(std::launch::async, [&] {
    const std::int16_t quiet_samples[]{-1, 2};
    audio_sink.OnData(quiet_samples, 16, 44'100, 1, 2, 2);
  });
  publish_second_callback.get();
  const auto second_audio_snapshot = audio_sink.snapshot();
  REQUIRE(second_audio_snapshot.callback_count == 2);
  REQUIRE(second_audio_snapshot.sample_rate == 44'100);
  REQUIRE(second_audio_snapshot.channels == 1);
  REQUIRE(second_audio_snapshot.peak == 32'768);
  REQUIRE(shareme::rtc::has_sufficient_movie_audio_reception(false, 0));
  REQUIRE(!shareme::rtc::has_sufficient_movie_audio_reception(true, 99));
  REQUIRE(shareme::rtc::has_sufficient_movie_audio_reception(true, 100));
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
  std::promise<std::string> movie_offer_promise;
  auto movie_offer_future = movie_offer_promise.get_future();
  shareme::rtc::SignaledPeerCallbacks movie_callbacks;
  movie_callbacks.description = [&](std::string type, std::string description) {
    if (type == "offer")
      movie_offer_promise.set_value(std::move(description));
  };
  auto movie_peer = shareme::rtc::SignaledPeer::create(
      {.role = SignaledRole::host,
       .movie_audio_source_factory =
           [fake_state] {
             return webrtc::scoped_refptr<shareme::rtc::LocalAudioSource>(
                 new FakeMovieAudioSource(fake_state));
           }},
      std::move(movie_callbacks));
  REQUIRE(movie_peer != nullptr);
  REQUIRE(movie_peer->start());
  REQUIRE(fake_state->started.load());
  REQUIRE(movie_offer_future.wait_for(std::chrono::seconds(5)) ==
          std::future_status::ready);
  const auto movie_offer = movie_offer_future.get();
  const auto host_voice_id = movie_offer.find("host-voice");
  const auto movie_audio_id = movie_offer.find("movie-audio");
  REQUIRE(host_voice_id != std::string::npos);
  REQUIRE(movie_audio_id != std::string::npos);
  REQUIRE(host_voice_id != movie_audio_id);
  REQUIRE(movie_offer.find("a=msid:shareme-test host-voice") !=
          std::string::npos);
  REQUIRE(movie_offer.find("a=msid:shareme-test movie-audio") !=
          std::string::npos);
  const auto audio_section = [&](std::size_t track_position) {
    const auto start = movie_offer.rfind("m=audio", track_position);
    const auto end = movie_offer.find("\r\nm=", track_position);
    REQUIRE(start != std::string::npos);
    return movie_offer.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
  };
  const auto voice_section = audio_section(host_voice_id);
  const auto movie_section = audio_section(movie_audio_id);
  REQUIRE(voice_section.find("stereo=1") == std::string::npos);
  REQUIRE(movie_section.find("stereo=1") != std::string::npos);
  REQUIRE(movie_section.find("sprop-stereo=1") != std::string::npos);
  movie_peer->cancel_wait();
  const auto movie_result = movie_peer->wait(std::chrono::seconds(1));
  REQUIRE(movie_result.movie_audio_chunks_generated == 123);
  movie_peer->stop();
  REQUIRE(fake_state->stop_count.load() == 1);
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
  REQUIRE(failing_state->stop_count.load() == 1);
  REQUIRE(sanitized_start_error == "movie-audio-source-unavailable");
  REQUIRE(sanitized_start_error.find("movie.mov") == std::string::npos);
  failing_movie_peer->stop();
  REQUIRE(failing_state->stop_count.load() == 1);
}
