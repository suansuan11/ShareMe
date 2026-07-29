#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>

namespace shareme::rtc {

class WebRtcRuntime;

enum class LoopbackPeer {
  left,
  right,
};

struct LoopbackNegotiationResult {
  bool ok{false};
  bool left_ice_connected{false};
  bool right_ice_connected{false};
  bool left_dtls_connected{false};
  bool right_dtls_connected{false};
  std::size_t drained_candidate_count{0};
  std::string error;
};

class LoopbackSignaling final {
public:
  explicit LoopbackSignaling(WebRtcRuntime &runtime);
  ~LoopbackSignaling();

  LoopbackSignaling(const LoopbackSignaling &) = delete;
  LoopbackSignaling &operator=(const LoopbackSignaling &) = delete;

  [[nodiscard]] LoopbackNegotiationResult
  negotiate(std::chrono::milliseconds timeout);
  [[nodiscard]] bool stage_candidate_for_test(LoopbackPeer destination,
                                              std::string candidate);
  [[nodiscard]] std::string failure() const;

  void stop() noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace shareme::rtc
