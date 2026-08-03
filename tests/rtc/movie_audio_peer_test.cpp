#include "shareme/rtc/movie_audio_peer.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
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
