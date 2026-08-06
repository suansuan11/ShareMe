#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "shareme/core/audio_output_contract.hpp"
#include "shareme/rtc/signaled_peer.hpp"

namespace shareme::rtc {

struct MovieAudioPeerConfig {
  SignaledRole role{SignaledRole::host};
  LocalAudioSourceFactory source_factory;
  bool native_playout{};
};

struct MovieAudioPeerResult {
  bool connected{};
  std::uint64_t frames_received{};
  std::uint64_t invalid_frames_received{};
  int sample_rate{};
  int channels{};
  int peak{};
  std::uint64_t chunks_generated{};
  std::string selected_candidate_type;
  std::string error;
};

struct MovieAudioPeerCallbacks {
  std::function<void(std::string type, std::string sdp)> description;
  std::function<void(std::string mid, int line, std::string candidate)>
      candidate;
  std::function<void(std::string category)> failure;
  // The PCM view is synchronous and non-owning; it is valid only for this call.
  // The callback must not synchronously re-enter peer lifecycle methods.
  std::function<void(shareme::core::AudioPcmBlockView pcm,
                     std::uint64_t receiver_sequence)>
      pcm;
};

class MovieAudioPeer final {
public:
  static std::unique_ptr<MovieAudioPeer>
  create(MovieAudioPeerConfig config, MovieAudioPeerCallbacks callbacks);
  ~MovieAudioPeer();
  MovieAudioPeer(const MovieAudioPeer &) = delete;
  MovieAudioPeer &operator=(const MovieAudioPeer &) = delete;

  [[nodiscard]] bool start();
  [[nodiscard]] bool receive_description(std::string type, std::string sdp);
  [[nodiscard]] bool receive_candidate(std::string mid, int line,
                                       std::string candidate);
  [[nodiscard]] MovieAudioPeerResult wait(std::chrono::milliseconds timeout);
  [[nodiscard]] LocalAudioSourceClockSnapshot
  source_clock_snapshot() const noexcept;
  void cancel_wait() noexcept;
  void stop() noexcept;

private:
  class Impl;
  explicit MovieAudioPeer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

} // namespace shareme::rtc
