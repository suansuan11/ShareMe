#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"

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

struct LoopbackPeerConnections {
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> left;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> right;
};

struct LoopbackMediaHooks {
  std::function<std::string(
      webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>,
      webrtc::scoped_refptr<webrtc::PeerConnectionInterface>,
      webrtc::scoped_refptr<webrtc::PeerConnectionInterface>)>
      configure;
  std::function<void(LoopbackPeer,
                     webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>)>
      remote_track;
};

class LoopbackSignaling final {
public:
  explicit LoopbackSignaling(WebRtcRuntime &runtime,
                             LoopbackMediaHooks hooks = {});
  ~LoopbackSignaling();

  LoopbackSignaling(const LoopbackSignaling &) = delete;
  LoopbackSignaling &operator=(const LoopbackSignaling &) = delete;

  [[nodiscard]] LoopbackNegotiationResult
  negotiate(std::chrono::milliseconds timeout);
  [[nodiscard]] bool stage_candidate_for_test(LoopbackPeer destination,
                                              std::string candidate);
  [[nodiscard]] std::string failure() const;
  [[nodiscard]] LoopbackNegotiationResult status() const;
  [[nodiscard]] LoopbackPeerConnections connections() const;

  void stop() noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace shareme::rtc
