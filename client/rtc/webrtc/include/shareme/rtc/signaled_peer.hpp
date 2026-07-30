#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace shareme::rtc {

enum class SignaledRole { host, viewer };
enum class SignaledAudioMode { synthetic, microphone };

struct SignaledPeerConfig {
  SignaledRole role{SignaledRole::host};
  SignaledAudioMode audio_mode{SignaledAudioMode::synthetic};
};

struct SignaledAudioPolicy {
  bool uses_native_microphone{false};
  bool processing_enabled{false};
};

struct SignaledPeerResult {
  bool connected{false};
  std::uint64_t video_frames_received{0};
  std::uint64_t audio_packets_sent{0};
  std::uint64_t audio_packets_received{0};
  std::optional<double> local_audio_level;
  std::string selected_candidate_type;
  std::string error;
};

struct SignaledPeerCallbacks {
  std::function<void(std::string type, std::string sdp)> description;
  std::function<void(std::string mid, int line, std::string candidate)>
      candidate;
  std::function<void(std::string category)> failure;
};

[[nodiscard]] SignaledAudioPolicy
signaled_audio_policy(SignaledAudioMode mode) noexcept;
[[nodiscard]] bool
valid_signaled_peer_config(const SignaledPeerConfig &config) noexcept;

[[nodiscard]] bool valid_remote_description(SignaledRole role,
                                            std::string_view type,
                                            std::string_view sdp) noexcept;
[[nodiscard]] bool valid_remote_candidate(std::string_view mid, int line,
                                          std::string_view candidate) noexcept;

class SignaledPeer final {
public:
  static std::unique_ptr<SignaledPeer> create(SignaledPeerConfig config,
                                              SignaledPeerCallbacks callbacks);
  static std::unique_ptr<SignaledPeer> create(SignaledRole role,
                                              SignaledPeerCallbacks callbacks);
  ~SignaledPeer();
  SignaledPeer(const SignaledPeer &) = delete;
  SignaledPeer &operator=(const SignaledPeer &) = delete;
  [[nodiscard]] bool start();
  [[nodiscard]] bool receive_description(std::string type, std::string sdp);
  [[nodiscard]] bool receive_candidate(std::string mid, int line,
                                       std::string candidate);
  [[nodiscard]] SignaledPeerResult wait(std::chrono::milliseconds timeout);
  void cancel_wait() noexcept;
  void stop() noexcept;

private:
  class Impl;
  explicit SignaledPeer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
} // namespace shareme::rtc
