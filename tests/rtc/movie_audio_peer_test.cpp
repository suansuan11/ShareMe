#include "shareme/rtc/movie_audio_peer.hpp"

#include "shareme/core/audio_output_contract.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "api/ref_counted_base.h"
#include "movie_audio_peer_policy.hpp"
#include "shareme/rtc/local_audio_source.hpp"

namespace {

void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

struct FakeAudioState {
  std::atomic_int stop_count{0};
};

class FakeMovieAudioSource final : private webrtc::RefCountedBase,
                                   public shareme::rtc::LocalAudioSource {
public:
  explicit FakeMovieAudioSource(std::shared_ptr<FakeAudioState> state)
      : state_(std::move(state)) {}

  bool start() override {
    if (worker_.joinable())
      return false;
    worker_ = std::jthread([this](std::stop_token stop_token) {
      std::vector<std::int16_t> pcm(480 * 2, 1'024);
      while (!stop_token.stop_requested()) {
        for (auto *sink : sinks_)
          sink->OnData(pcm.data(), 16, 48'000, 2, 480, chunks_generated_ * 10);
        ++chunks_generated_;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    });
    return true;
  }

  void stop() noexcept override {
    if (stopped_.exchange(true))
      return;
    if (worker_.joinable()) {
      worker_.request_stop();
      worker_.join();
    }
    state_->stop_count.fetch_add(1);
  }

  std::uint64_t generated_count() const noexcept override {
    return chunks_generated_.load();
  }
  std::optional<std::int64_t> last_pts_ms() const noexcept override {
    return static_cast<std::int64_t>(chunks_generated_.load() * 10);
  }
  shareme::rtc::LocalAudioSourceClockSnapshot
  clock_snapshot() const noexcept override {
    return {.source_sequence = chunks_generated_.load(),
            .media_pts_ms = last_pts_ms(),
            .generation = 3,
            .audio_epoch = 7,
            .sample_rate = 48'000,
            .channel_count = 2};
  }
  std::string error() const override { return {}; }
  void AddSink(webrtc::AudioTrackSinkInterface *sink) override {
    if (sink)
      sinks_.push_back(sink);
  }
  void RemoveSink(webrtc::AudioTrackSinkInterface *sink) override {
    std::erase(sinks_, sink);
  }
  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  ~FakeMovieAudioSource() override { stop(); }

  std::shared_ptr<FakeAudioState> state_;
  std::jthread worker_;
  std::vector<webrtc::AudioTrackSinkInterface *> sinks_;
  std::atomic_bool stopped_{false};
  std::atomic_uint64_t chunks_generated_{0};
};

} // namespace

int main() {
  using shareme::rtc::LocalAudioSourceFactory;
  using shareme::rtc::MovieAudioPeer;
  using shareme::rtc::MovieAudioPeerCallbacks;
  using shareme::rtc::MovieAudioPeerConfig;
  using shareme::rtc::SignaledRole;

  auto fake_state = std::make_shared<FakeAudioState>();
  LocalAudioSourceFactory fake_factory = [fake_state] {
    return webrtc::scoped_refptr<shareme::rtc::LocalAudioSource>(
        new FakeMovieAudioSource(fake_state));
  };

  REQUIRE(MovieAudioPeer::create(
              MovieAudioPeerConfig{.role = SignaledRole::host}, {}) ==
          nullptr);
  REQUIRE(MovieAudioPeer::create(
              MovieAudioPeerConfig{.role = SignaledRole::viewer,
                                   .source_factory = fake_factory},
              {}) == nullptr);
  REQUIRE(MovieAudioPeer::create(
              MovieAudioPeerConfig{.role = SignaledRole::host,
                                   .source_factory = fake_factory,
                                   .native_playout = true},
              {}) == nullptr);

  std::unique_ptr<MovieAudioPeer> host;
  std::unique_ptr<MovieAudioPeer> viewer;
  std::string offer;
  std::atomic_uint64_t pcm_callback_count{0};
  std::atomic_uint64_t last_pcm_sequence{0};
  std::atomic_bool pcm_metadata_valid{true};
  std::atomic_bool throw_from_pcm_callback{true};
  MovieAudioPeerCallbacks host_callbacks;
  host_callbacks.description = [&](std::string type, std::string sdp) {
    if (type == "offer")
      offer = sdp;
    REQUIRE(viewer->receive_description(std::move(type), std::move(sdp)));
  };
  host_callbacks.candidate = [&](std::string mid, int line,
                                 std::string candidate) {
    REQUIRE(viewer->receive_candidate(std::move(mid), line,
                                      std::move(candidate)));
  };
  MovieAudioPeerCallbacks viewer_callbacks;
  viewer_callbacks.description = [&](std::string type, std::string sdp) {
    REQUIRE(host->receive_description(std::move(type), std::move(sdp)));
  };
  viewer_callbacks.candidate = [&](std::string mid, int line,
                                    std::string candidate) {
    REQUIRE(host->receive_candidate(std::move(mid), line,
                                      std::move(candidate)));
  };
  viewer_callbacks.pcm = [&](shareme::core::AudioPcmBlockView pcm,
                             std::uint64_t receiver_sequence) {
    if (!shareme::core::is_valid_audio_pcm_block(pcm) ||
        pcm.receiver_sequence != receiver_sequence || pcm.frame_count != 480 ||
        pcm.sample_rate != 48'000 || pcm.channel_count != 2 ||
        pcm.payload.bytes.size_bytes() != 480U * 4U)
      pcm_metadata_valid.store(false, std::memory_order_relaxed);
    last_pcm_sequence.store(receiver_sequence, std::memory_order_relaxed);
    pcm_callback_count.fetch_add(1, std::memory_order_release);
    if (throw_from_pcm_callback.exchange(false, std::memory_order_acq_rel))
      throw std::runtime_error("test callback failure");
  };

  viewer = MovieAudioPeer::create({.role = SignaledRole::viewer},
                                  std::move(viewer_callbacks));
  host = MovieAudioPeer::create(
      {.role = SignaledRole::host, .source_factory = std::move(fake_factory)},
      std::move(host_callbacks));
  REQUIRE(viewer != nullptr);
  REQUIRE(host != nullptr);
  REQUIRE(viewer->start());
  REQUIRE(host->start());

  const auto viewer_result = viewer->wait(std::chrono::seconds(10));
  REQUIRE(viewer_result.connected);
  REQUIRE(viewer_result.frames_received >= 100);
  REQUIRE(viewer_result.invalid_frames_received == 0);
  REQUIRE(viewer_result.sample_rate == 48'000);
  REQUIRE(viewer_result.channels == 2);
  REQUIRE(viewer_result.peak > 0);
  REQUIRE(pcm_callback_count.load(std::memory_order_acquire) >= 100);
  REQUIRE(pcm_metadata_valid.load(std::memory_order_relaxed));
  REQUIRE(last_pcm_sequence.load(std::memory_order_relaxed) > 0);
  REQUIRE(!offer.empty());
  const auto audio_mline = offer.find("m=audio");
  REQUIRE(audio_mline != std::string::npos);
  REQUIRE(offer.find("m=audio", audio_mline + 1) == std::string::npos);
  REQUIRE(offer.find("stereo=1") != std::string::npos);
  REQUIRE(offer.find("sprop-stereo=1") != std::string::npos);

  const auto host_result = host->wait(std::chrono::seconds(2));
  REQUIRE(host_result.connected);
  REQUIRE(host_result.chunks_generated >= 100);
  REQUIRE(host_result.error.empty());
  const auto source_snapshot = host->source_clock_snapshot();
  REQUIRE(source_snapshot.source_sequence >= host_result.chunks_generated);
  REQUIRE(source_snapshot.media_pts_ms.has_value());
  REQUIRE(source_snapshot.generation == 3);
  REQUIRE(source_snapshot.audio_epoch == 7);
  REQUIRE(source_snapshot.sample_rate == 48'000);
  REQUIRE(source_snapshot.channel_count == 2);

  host->stop();
  host->stop();
  viewer->stop();
  viewer->stop();
  REQUIRE(fake_state->stop_count.load() == 1);

  REQUIRE(shareme::rtc::movie_audio_device_mode(
              SignaledRole::viewer, true) ==
          shareme::rtc::AudioDeviceMode::playout);
  REQUIRE(shareme::rtc::movie_audio_device_mode(
              SignaledRole::viewer, false) ==
          shareme::rtc::AudioDeviceMode::synthetic);
  REQUIRE(shareme::rtc::movie_audio_device_mode(SignaledRole::host, false) ==
          shareme::rtc::AudioDeviceMode::synthetic);
}
