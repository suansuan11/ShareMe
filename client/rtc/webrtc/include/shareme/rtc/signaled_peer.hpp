#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace shareme::rtc {

enum class SignaledRole { host, viewer };

struct SignaledPeerResult {
  bool connected{false};
  std::uint64_t video_frames_received{0};
  std::uint64_t audio_packets_sent{0};
  std::uint64_t audio_packets_received{0};
  std::string selected_candidate_type;
  std::string error;
};

struct SignaledPeerCallbacks {
  std::function<void(std::string type, std::string sdp)> description;
  std::function<void(std::string mid, int line, std::string candidate)> candidate;
};

[[nodiscard]] bool valid_remote_description(SignaledRole role,
                                             std::string_view type,
                                             std::string_view sdp) noexcept;
[[nodiscard]] bool valid_remote_candidate(std::string_view mid, int line,
                                           std::string_view candidate) noexcept;

class SignaledPeer final {
public:
  static std::unique_ptr<SignaledPeer> create(SignaledRole role,
                                               SignaledPeerCallbacks callbacks);
  ~SignaledPeer();
  SignaledPeer(const SignaledPeer&) = delete;
  SignaledPeer& operator=(const SignaledPeer&) = delete;
  [[nodiscard]] bool start();
  [[nodiscard]] bool receive_description(std::string type, std::string sdp);
  [[nodiscard]] bool receive_candidate(std::string mid, int line,
                                       std::string candidate);
  [[nodiscard]] SignaledPeerResult wait(std::chrono::milliseconds timeout);
  void stop() noexcept;
private:
  class Impl;
  explicit SignaledPeer(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}
