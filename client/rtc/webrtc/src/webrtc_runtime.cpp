#include "webrtc_runtime.hpp"

#include <condition_variable>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment_factory.h"
#include "api/peer_connection_interface.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#if defined(_WIN32)
#include "rtc_base/win32_socket_init.h"
#endif

namespace shareme::rtc {

namespace {

class CombinedVideoDecoderFactory final : public webrtc::VideoDecoderFactory {
public:
  explicit CombinedVideoDecoderFactory(
      std::vector<std::unique_ptr<webrtc::VideoDecoderFactory>> factories)
      : factories_(std::move(factories)) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::vector<webrtc::SdpVideoFormat> formats;
    for (const auto &factory : factories_) {
      auto supported = factory->GetSupportedFormats();
      formats.insert(formats.end(), supported.begin(), supported.end());
    }
    return formats;
  }

  CodecSupport QueryCodecSupport(
      const webrtc::SdpVideoFormat &format, bool reference_scaling,
      std::optional<webrtc::Resolution> resolution) const override {
    for (const auto &factory : factories_) {
      const auto support = factory->QueryCodecSupport(
          format, reference_scaling, resolution);
      if (support.is_supported)
        return support;
    }
    return {};
  }

  std::unique_ptr<webrtc::VideoDecoder> Create(
      const webrtc::Environment &environment,
      const webrtc::SdpVideoFormat &format) override {
    for (const auto &factory : factories_) {
      if (!factory->QueryCodecSupport(format, false, std::nullopt).is_supported)
        continue;
      if (auto decoder = factory->Create(environment, format))
        return decoder;
    }
    return nullptr;
  }

private:
  std::vector<std::unique_ptr<webrtc::VideoDecoderFactory>> factories_;
};

std::unique_ptr<webrtc::VideoDecoderFactory> create_video_decoder_factory() {
  auto vp8_factory = std::make_unique<webrtc::VideoDecoderFactoryTemplate<
      webrtc::LibvpxVp8DecoderTemplateAdapter>>();
  auto platform_factory = create_platform_video_decoder_factory();
  if (platform_factory == nullptr)
    return vp8_factory;

  std::vector<std::unique_ptr<webrtc::VideoDecoderFactory>> factories;
  factories.push_back(std::move(platform_factory));
  factories.push_back(std::move(vp8_factory));
  return std::make_unique<CombinedVideoDecoderFactory>(std::move(factories));
}

} // namespace

#if !defined(__APPLE__)
std::unique_ptr<webrtc::VideoDecoderFactory>
create_platform_video_decoder_factory() {
  return nullptr;
}
#endif

class WebRtcRuntime::ShutdownHook final {
public:
  explicit ShutdownHook(std::function<void()> shutdown)
      : shutdown(std::move(shutdown)) {}

  std::mutex mutex;
  std::condition_variable completed;
  std::function<void()> shutdown;
  bool cancelled{false};
  bool running{false};
};

std::shared_ptr<WebRtcRuntime> WebRtcRuntime::create(
    webrtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device,
    std::unique_ptr<webrtc::VideoEncoderFactory> video_encoder_factory) {
  auto runtime = std::shared_ptr<WebRtcRuntime>(new WebRtcRuntime(),
                                                &WebRtcRuntime::destroy);
  if (!runtime->start(std::move(audio_device), std::move(video_encoder_factory))) {
    return nullptr;
  }
  return runtime;
}

WebRtcRuntime::~WebRtcRuntime() { static_cast<void>(stop()); }

void WebRtcRuntime::destroy(WebRtcRuntime *runtime) noexcept {
  auto completed = runtime->destruction_completed_;
  if (runtime->on_owned_thread()) {
    std::thread([runtime, completed] {
      delete runtime;
      completed->set_value();
    }).detach();
    return;
  }
  delete runtime;
  completed->set_value();
}

webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
WebRtcRuntime::factory() const {
  return factory_;
}

webrtc::Thread *WebRtcRuntime::signaling_thread() const noexcept {
  return signaling_thread_.get();
}

bool WebRtcRuntime::threads_running() const noexcept {
  return running_.load(std::memory_order_acquire) &&
         network_thread_ != nullptr && worker_thread_ != nullptr &&
         signaling_thread_ != nullptr && network_thread_->RunningForTest() &&
         worker_thread_->RunningForTest() &&
         signaling_thread_->RunningForTest();
}

std::shared_future<void> WebRtcRuntime::destruction_completed() const {
  return destruction_future_;
}

bool WebRtcRuntime::on_owned_thread() const noexcept {
  return (network_thread_ != nullptr && network_thread_->IsCurrent()) ||
         (worker_thread_ != nullptr && worker_thread_->IsCurrent()) ||
         (signaling_thread_ != nullptr && signaling_thread_->IsCurrent());
}

WebRtcRuntime::ShutdownHookHandle
WebRtcRuntime::register_shutdown_hook(void *owner,
                                      std::function<void()> shutdown) {
  std::lock_guard lock(shutdown_hooks_mutex_);
  if (stopping_ || !running_.load(std::memory_order_acquire)) {
    return nullptr;
  }
  auto hook = std::make_shared<ShutdownHook>(std::move(shutdown));
  if (!shutdown_hooks_.emplace(owner, hook).second) {
    return nullptr;
  }
  return hook;
}

void WebRtcRuntime::unregister_shutdown_hook(
    void *owner, const ShutdownHookHandle &hook) noexcept {
  {
    std::lock_guard lock(shutdown_hooks_mutex_);
    const auto found = shutdown_hooks_.find(owner);
    if (found != shutdown_hooks_.end() && found->second == hook) {
      shutdown_hooks_.erase(found);
    }
  }
  std::unique_lock hook_lock(hook->mutex);
  hook->cancelled = true;
  hook->completed.wait(hook_lock, [&] { return !hook->running; });
}

bool WebRtcRuntime::start(
    webrtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device,
    std::unique_ptr<webrtc::VideoEncoderFactory> video_encoder_factory) {
#if defined(_WIN32)
  winsock_initializer_ = std::make_unique<webrtc::WinsockInitializer>();
  if (winsock_initializer_->error() != 0) {
    winsock_initializer_.reset();
    return false;
  }
#endif
  if (!webrtc::InitializeSSL()) {
#if defined(_WIN32)
    winsock_initializer_.reset();
#endif
    return false;
  }
  ssl_initialized_ = true;

  network_thread_ = webrtc::Thread::CreateWithSocketServer();
  worker_thread_ = webrtc::Thread::Create();
  signaling_thread_ = webrtc::Thread::Create();
  if (network_thread_ == nullptr || worker_thread_ == nullptr ||
      signaling_thread_ == nullptr) {
    static_cast<void>(stop());
    return false;
  }

  network_thread_->SetName("ShareMeNetwork", nullptr);
  worker_thread_->SetName("ShareMeWorker", nullptr);
  signaling_thread_->SetName("ShareMeSignaling", nullptr);
  if (!network_thread_->Start() || !worker_thread_->Start() ||
      !signaling_thread_->Start()) {
    static_cast<void>(stop());
    return false;
  }

  signaling_thread_->BlockingCall([&] {
    webrtc::PeerConnectionFactoryDependencies dependencies;
    dependencies.network_thread = network_thread_.get();
    dependencies.worker_thread = worker_thread_.get();
    dependencies.signaling_thread = signaling_thread_.get();
    dependencies.socket_factory = network_thread_->socketserver();
    dependencies.adm = std::move(audio_device);
    dependencies.audio_encoder_factory =
        webrtc::CreateBuiltinAudioEncoderFactory();
    dependencies.audio_decoder_factory =
        webrtc::CreateBuiltinAudioDecoderFactory();
    dependencies.video_encoder_factory =
        video_encoder_factory != nullptr
            ? std::move(video_encoder_factory)
            : std::make_unique<webrtc::VideoEncoderFactoryTemplate<
                  webrtc::LibvpxVp8EncoderTemplateAdapter>>();
    dependencies.video_decoder_factory = create_video_decoder_factory();
    webrtc::EnableMedia(dependencies);
    factory_ =
        webrtc::CreateModularPeerConnectionFactory(std::move(dependencies));
  });

  if (factory_ == nullptr) {
    static_cast<void>(stop());
    return false;
  }
  running_.store(true, std::memory_order_release);
  return true;
}

bool WebRtcRuntime::stop() noexcept {
  if (on_owned_thread()) {
    return false;
  }

  std::vector<ShutdownHookHandle> shutdown_hooks;
  {
    std::unique_lock lock(shutdown_hooks_mutex_);
    if (stopped_) {
      return true;
    }
    if (stopping_) {
      shutdown_completed_.wait(lock, [this] { return stopped_; });
      return true;
    }
    stopping_ = true;
    shutdown_hooks.reserve(shutdown_hooks_.size());
    for (auto &[owner, hook] : shutdown_hooks_) {
      static_cast<void>(owner);
      shutdown_hooks.push_back(std::move(hook));
    }
    shutdown_hooks_.clear();
  }
  for (const auto &hook : shutdown_hooks) {
    std::function<void()> shutdown;
    {
      std::lock_guard hook_lock(hook->mutex);
      if (hook->cancelled) {
        continue;
      }
      hook->running = true;
      shutdown = hook->shutdown;
    }
    shutdown();
    {
      std::lock_guard hook_lock(hook->mutex);
      hook->running = false;
      hook->completed.notify_all();
    }
  }

  running_.store(false, std::memory_order_release);
  if (signaling_thread_ != nullptr && signaling_thread_->RunningForTest()) {
    signaling_thread_->BlockingCall([this] { factory_ = nullptr; });
  } else {
    factory_ = nullptr;
  }

  if (signaling_thread_ != nullptr) {
    signaling_thread_->Stop();
  }
  if (worker_thread_ != nullptr) {
    worker_thread_->Stop();
  }
  if (network_thread_ != nullptr) {
    network_thread_->Stop();
  }
  signaling_thread_.reset();
  worker_thread_.reset();
  network_thread_.reset();

#if defined(_WIN32)
  winsock_initializer_.reset();
#endif

  if (ssl_initialized_) {
    webrtc::CleanupSSL();
    ssl_initialized_ = false;
  }
  {
    std::lock_guard lock(shutdown_hooks_mutex_);
    stopped_ = true;
    shutdown_completed_.notify_all();
  }
  return true;
}

} // namespace shareme::rtc
