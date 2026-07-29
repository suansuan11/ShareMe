#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "modules/audio_device/include/audio_device.h"

namespace webrtc {
class Thread;
} // namespace webrtc

namespace shareme::rtc {

class WebRtcRuntime final {
public:
  class ShutdownHook;
  using ShutdownHookHandle = std::shared_ptr<ShutdownHook>;

  static std::shared_ptr<WebRtcRuntime> create(
      webrtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device = nullptr);

  ~WebRtcRuntime();

  WebRtcRuntime(const WebRtcRuntime &) = delete;
  WebRtcRuntime &operator=(const WebRtcRuntime &) = delete;

  [[nodiscard]] webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
  factory() const;
  [[nodiscard]] webrtc::Thread *signaling_thread() const noexcept;
  [[nodiscard]] bool threads_running() const noexcept;
  [[nodiscard]] std::shared_future<void> destruction_completed() const;

  [[nodiscard]] ShutdownHookHandle
  register_shutdown_hook(void *owner, std::function<void()> shutdown);
  void unregister_shutdown_hook(void *owner,
                                const ShutdownHookHandle &hook) noexcept;
  [[nodiscard]] bool stop() noexcept;

private:
  WebRtcRuntime() = default;

  bool start(webrtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device);
  static void destroy(WebRtcRuntime *runtime) noexcept;
  [[nodiscard]] bool on_owned_thread() const noexcept;

  std::unique_ptr<webrtc::Thread> network_thread_;
  std::unique_ptr<webrtc::Thread> worker_thread_;
  std::unique_ptr<webrtc::Thread> signaling_thread_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  std::atomic_bool running_{false};
  std::mutex shutdown_hooks_mutex_;
  std::condition_variable shutdown_completed_;
  std::unordered_map<void *, ShutdownHookHandle> shutdown_hooks_;
  bool stopping_{false};
  bool stopped_{false};
  bool ssl_initialized_{false};
  std::shared_ptr<std::promise<void>> destruction_completed_{
      std::make_shared<std::promise<void>>()};
  std::shared_future<void> destruction_future_{
      destruction_completed_->get_future().share()};
};

} // namespace shareme::rtc
