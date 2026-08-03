#include "shareme/rtc/signaled_peer.hpp"

#include <atomic>
#include <condition_variable>
#include <future>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/data_channel_interface.h"
#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/ref_counted_base.h"
#include "api/rtp_receiver_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/units/time_delta.h"
#include "audio_device_factory.hpp"
#include "counting_audio_sink.hpp"
#include "remote_video_sink.hpp"
#include "microphone_permission.hpp"
#include "pc/session_description.h"
#include "rtc_base/thread.h"
#include "shareme/rtc/candidate_stager.hpp"
#include "test_pattern_source.hpp"
#include "webrtc_runtime.hpp"

namespace shareme::rtc {
namespace {
constexpr std::string_view kControlChannelLabel{"shareme-control-v1"};
constexpr std::size_t kMaximumControlMessageBytes = 64 * 1024;
struct PendingCandidate {
  std::string mid;
  int line{};
  std::string candidate;
};

[[nodiscard]] std::string
sanitized_movie_audio_error(const std::string &error) {
  if (error == "movie-audio-open-failed" ||
      error == "movie-audio-unavailable" ||
      error == "movie-audio-decode-failed" ||
      error == "movie-audio-frame-invalid") {
    return error;
  }
  return "movie-audio-source-unavailable";
}

[[nodiscard]] std::optional<std::int64_t>
checked_absolute_difference(std::int64_t left, std::int64_t right) noexcept {
  const auto difference = left >= right ? static_cast<std::uint64_t>(left) -
                                              static_cast<std::uint64_t>(right)
                                        : static_cast<std::uint64_t>(right) -
                                              static_cast<std::uint64_t>(left);
  if (difference >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(difference);
}

class CreateObserver final : public webrtc::CreateSessionDescriptionObserver,
                             private webrtc::RefCountedBase {
public:
  using Success =
      std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface>)>;
  CreateObserver(Success success, std::function<void(std::string)> failure)
      : success_(std::move(success)), failure_(std::move(failure)) {}
  void OnSuccess(webrtc::SessionDescriptionInterface *value) override {
    success_(std::unique_ptr<webrtc::SessionDescriptionInterface>(value));
  }
  void OnFailure(webrtc::RTCError error) override {
    failure_(std::string(error.message()));
  }
  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  ~CreateObserver() override = default;
  Success success_;
  std::function<void(std::string)> failure_;
};
class SetObserver final : public webrtc::SetSessionDescriptionObserver,
                          private webrtc::RefCountedBase {
public:
  SetObserver(std::function<void()> success,
              std::function<void(std::string)> failure)
      : success_(std::move(success)), failure_(std::move(failure)) {}
  void OnSuccess() override { success_(); }
  void OnFailure(webrtc::RTCError error) override {
    failure_(std::string(error.message()));
  }
  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  ~SetObserver() override = default;
  std::function<void()> success_;
  std::function<void(std::string)> failure_;
};
class StatsObserver final : public webrtc::RTCStatsCollectorCallback,
                            private webrtc::RefCountedBase {
public:
  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport> &report)
      override {
    std::lock_guard lock(mu_);
    report_ = report;
    ready_ = true;
    cv_.notify_all();
  }
  auto wait() {
    std::unique_lock lock(mu_);
    cv_.wait_for(lock, std::chrono::seconds(2), [&] { return ready_; });
    return report_;
  }
  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  ~StatsObserver() override = default;
  std::mutex mu_;
  std::condition_variable cv_;
  bool ready_{};
  webrtc::scoped_refptr<const webrtc::RTCStatsReport> report_;
};
} // namespace

bool valid_remote_description(SignaledRole role, std::string_view type,
                              std::string_view sdp) noexcept {
  return !sdp.empty() && ((role == SignaledRole::viewer && type == "offer") ||
                          (role == SignaledRole::host && type == "answer"));
}
SignaledAudioPolicy signaled_audio_policy(SignaledAudioMode mode) noexcept {
  switch (mode) {
  case SignaledAudioMode::synthetic:
    return {};
  case SignaledAudioMode::microphone:
    return {.uses_native_microphone = true, .processing_enabled = true};
  }
  return {};
}
bool valid_signaled_peer_config(const SignaledPeerConfig &config) noexcept {
  const auto valid_role =
      config.role == SignaledRole::host || config.role == SignaledRole::viewer;
  const auto valid_audio = config.audio_mode == SignaledAudioMode::synthetic ||
                           config.audio_mode == SignaledAudioMode::microphone;
  const auto valid_video = config.video_mode == SignaledVideoMode::synthetic ||
                           (config.video_mode == SignaledVideoMode::injected &&
                            static_cast<bool>(config.video_source_factory));
  const auto valid_movie_audio =
      !config.movie_audio_source_factory || config.role == SignaledRole::host;
  return valid_role && valid_audio && valid_video && valid_movie_audio;
}
bool valid_remote_candidate(std::string_view mid, int line,
                            std::string_view candidate) noexcept {
  return !mid.empty() && line >= 0 && !candidate.empty();
}
bool valid_control_message(std::string_view message) noexcept {
  return !message.empty() && message.size() <= kMaximumControlMessageBytes;
}
bool valid_control_channel(std::string_view label, bool ordered,
                           bool reliable) noexcept {
  return label == kControlChannelLabel && ordered && reliable;
}
bool is_expected_voice_rtp_track(SignaledRole role, bool outbound,
                                 std::string_view track_identifier) noexcept {
  const auto local =
      role == SignaledRole::host ? "host-voice" : "viewer-voice";
  const auto remote =
      role == SignaledRole::host ? "viewer-voice" : "host-voice";
  return track_identifier == (outbound ? local : remote);
}
bool is_expected_inbound_voice_rtp_track(
    SignaledRole role, std::string_view track_identifier,
    std::string_view stats_mid, std::string_view expected_voice_mid) noexcept {
  return is_expected_voice_rtp_track(role, false, track_identifier) ||
         (!expected_voice_mid.empty() && stats_mid == expected_voice_mid);
}

class SignaledPeer::Impl final {
public:
  Impl(SignaledPeerConfig config, SignaledPeerCallbacks callbacks)
      : config_(std::move(config)), role_(config_.role),
        callbacks_(std::move(callbacks)),
        sink_(config_.remote_video_frame) {}
  bool initialize() {
    if (!valid_signaled_peer_config(config_)) {
      if (config_.role != SignaledRole::host &&
          config_.role != SignaledRole::viewer)
        fail("invalid-role");
      else if (config_.audio_mode != SignaledAudioMode::synthetic &&
               config_.audio_mode != SignaledAudioMode::microphone)
        fail("invalid-audio-mode");
      else if (config_.movie_audio_source_factory &&
               config_.role != SignaledRole::host)
        fail("invalid-movie-audio-config");
      else
        fail("invalid-video-mode");
      return false;
    }
    const auto policy = signaled_audio_policy(config_.audio_mode);
    const auto mode = policy.uses_native_microphone
                          ? AudioDeviceMode::microphone
                          : AudioDeviceMode::synthetic;
    auto audio =
        create_audio_device(webrtc::CreateEnvironment(), mode, {}, {}, [] {
          return platform_microphone_permission_status();
        });
    if (!audio.ok()) {
      switch (audio.error) {
      case AudioDeviceError::permission_denied:
        fail("permission-denied");
        break;
      case AudioDeviceError::dependency_unavailable:
        fail("dependency-unavailable");
        break;
      case AudioDeviceError::initialization_failed:
      case AudioDeviceError::none:
        fail("audio-initialization-failed");
        break;
      }
      return false;
    }
    runtime_ = WebRtcRuntime::create(audio.device);
    if (!runtime_) {
      fail("WebRTC runtime creation failed");
      return false;
    }
    queues_ = webrtc::CreateDefaultTaskQueueFactory();
    if (config_.video_mode == SignaledVideoMode::injected)
      video_source_ = config_.video_source_factory(*queues_);
    else
      video_source_ = TestPatternSource::create(*queues_, 640, 360, 30);
    if (!video_source_) {
      fail("video-source-unavailable");
      return false;
    }
    if (config_.movie_audio_source_factory) {
      movie_audio_source_ = config_.movie_audio_source_factory();
      if (!movie_audio_source_) {
        fail("movie-audio-source-unavailable");
        return false;
      }
    }
    return runtime_->signaling_thread()->BlockingCall(
        [this] { return setup(); });
  }
  bool start() {
    if (!runtime_ || !peer_ || !video_source_)
      return false;
    if (!video_source_->start()) {
      const auto source_error = video_source_->error();
      fail(source_error.empty() ? "video-source-start-failed" : source_error);
      return false;
    }
    if (movie_audio_source_ && !movie_audio_source_->start()) {
      const auto source_error =
          sanitized_movie_audio_error(movie_audio_source_->error());
      stop_movie_audio_source();
      video_source_->stop();
      fail(source_error);
      return false;
    }
    if (role_ == SignaledRole::host)
      runtime_->signaling_thread()->PostTask([this] { create_offer(); });
    return true;
  }
  bool receive_description(std::string type, std::string sdp) {
    if (!valid_remote_description(role_, type, sdp) || !runtime_)
      return false;
    runtime_->signaling_thread()->PostTask(
        [this, type = std::move(type), sdp = std::move(sdp)]() mutable {
          apply_remote(std::move(type), std::move(sdp));
        });
    return true;
  }
  bool receive_candidate(std::string mid, int line, std::string candidate) {
    if (!valid_remote_candidate(mid, line, candidate) || !runtime_)
      return false;
    runtime_->signaling_thread()->PostTask(
        [this, p = PendingCandidate{std::move(mid), line,
                                    std::move(candidate)}]() mutable {
          if (remote_set_)
            add_candidate(std::move(p));
          else if (!candidates_.stage(std::move(p)))
            fail("pending ICE candidate limit exceeded");
        });
    return true;
  }
  bool send_control_message(std::string message) {
    if (role_ != SignaledRole::host || !valid_control_message(message) ||
        !runtime_ || !runtime_->signaling_thread() ||
        !runtime_->signaling_thread()->RunningForTest())
      return false;
    return runtime_->signaling_thread()->BlockingCall(
        [this, message = std::move(message)] {
          if (!control_channel_ || control_channel_->state() !=
                                       webrtc::DataChannelInterface::kOpen)
            return false;
          return control_channel_->Send(webrtc::DataBuffer(message));
        });
  }
  SignaledPeerResult wait(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::optional<std::chrono::steady_clock::time_point> media_ready;
    while (std::chrono::steady_clock::now() < deadline) {
      if (wait_cancelled_.load(std::memory_order_acquire))
        break;
      const auto source_error = video_source_->error();
      if (!source_error.empty()) {
        fail(source_error);
        break;
      }
      if (movie_audio_source_) {
        const auto movie_audio_error = movie_audio_source_->error();
        if (!movie_audio_error.empty()) {
          fail(sanitized_movie_audio_error(movie_audio_error));
          break;
        }
      }
      {
        std::lock_guard lock(mu_);
        if (!result_.error.empty())
          break;
        if (result_.connected && sink_.frame_count() > 0) {
          const auto movie_audio_ready = has_sufficient_movie_audio_reception(
              expected_remote_movie_audio_.load(std::memory_order_acquire),
              remote_movie_audio_seen_.load(std::memory_order_acquire),
              movie_audio_sink_.valid_callback_count());
          if (movie_audio_ready && !media_ready)
            media_ready = std::chrono::steady_clock::now();
          else if (!movie_audio_ready)
            media_ready.reset();
        }
      }
      if (media_ready && std::chrono::steady_clock::now() - *media_ready >=
                             std::chrono::seconds(2))
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (wait_cancelled_.load(std::memory_order_acquire)) {
      std::lock_guard lock(mu_);
      populate_media_result();
      if (result_.error.empty())
        result_.error = "signaled call cancelled";
      return result_;
    }
    collect_stats();
    std::lock_guard lock(mu_);
    populate_media_result();
    if (!has_sufficient_movie_audio_reception(
            expected_remote_movie_audio_.load(std::memory_order_acquire),
            remote_movie_audio_seen_.load(std::memory_order_acquire),
            result_.movie_audio_frames_received) &&
        result_.error.empty()) {
      result_.error = "movie-audio-unavailable";
    }
    if (!result_.connected && result_.error.empty())
      result_.error = "signaled call timed out";
    if (result_.video_frames_received == 0 && result_.error.empty())
      result_.error = "no remote video received";
    if ((result_.audio_packets_sent == 0 ||
         result_.audio_packets_received == 0) &&
        result_.error.empty())
      result_.error = "no bidirectional audio RTP";
    return result_;
  }
  void cancel_wait() noexcept {
    wait_cancelled_.store(true, std::memory_order_release);
  }
  void stop() {
    if (stopped_.exchange(true))
      return;
    callbacks_active_->store(false, std::memory_order_release);
    sink_.clear_callback();
    stop_movie_audio_source();
    if (video_source_)
      video_source_->stop();
    if (runtime_ && runtime_->signaling_thread() &&
        runtime_->signaling_thread()->RunningForTest())
      runtime_->signaling_thread()->BlockingCall([this] {
        if (remote_video_)
          remote_video_->RemoveSink(&sink_);
        if (remote_movie_audio_)
          remote_movie_audio_->RemoveSink(&movie_audio_sink_);
        if (control_channel_ && control_observer_)
          control_channel_->UnregisterObserver();
        control_observer_.reset();
        control_channel_ = nullptr;
        peer_ = nullptr;
        remote_video_ = nullptr;
        remote_audio_ = nullptr;
        remote_movie_audio_ = nullptr;
        video_track_ = nullptr;
        audio_track_ = nullptr;
        movie_audio_track_ = nullptr;
        audio_source_ = nullptr;
        observer_.reset();
      });
    video_source_ = nullptr;
    movie_audio_source_ = nullptr;
    if (runtime_)
      static_cast<void>(runtime_->stop());
  }
  ~Impl() { stop(); }

private:
  class ControlObserver final : public webrtc::DataChannelObserver {
  public:
    explicit ControlObserver(Impl &owner) : owner_(owner) {}
    void OnStateChange() override {}
    void OnMessage(const webrtc::DataBuffer &buffer) override {
      if (!owner_.callbacks_active_->load(std::memory_order_acquire) ||
          buffer.binary || buffer.data.empty() ||
          buffer.data.size() > kMaximumControlMessageBytes)
        return;
      const auto message = std::string(
          reinterpret_cast<const char *>(buffer.data.data()), buffer.data.size());
      if (owner_.config_.control_message)
        owner_.config_.control_message(message);
    }
    void OnBufferedAmountChange(std::uint64_t) override {}

  private:
    Impl &owner_;
  };
  class PeerObserver final : public webrtc::PeerConnectionObserver {
  public:
    explicit PeerObserver(Impl &owner) : owner_(owner) {}
    void OnSignalingChange(
        webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(
        webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {
      owner_.on_data_channel(std::move(channel));
    }
    void OnIceGatheringChange(
        webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnIceCandidate(const webrtc::IceCandidate *value) override {
      if (value && owner_.callbacks_.candidate)
        owner_.callbacks_.candidate(value->sdp_mid(), value->sdp_mline_index(),
                                    value->ToString());
    }
    void OnIceConnectionChange(
        webrtc::PeerConnectionInterface::IceConnectionState state) override {
      if (state == webrtc::PeerConnectionInterface::kIceConnectionFailed)
        owner_.fail("ICE connection failed");
    }
    void OnConnectionChange(
        webrtc::PeerConnectionInterface::PeerConnectionState state) override {
      if (state ==
          webrtc::PeerConnectionInterface::PeerConnectionState::kConnected) {
        {
          std::lock_guard lock(owner_.mu_);
          owner_.result_.connected = true;
        }
        owner_.runtime_->signaling_thread()->PostDelayedTask(
            [owner = &owner_, active = owner_.callbacks_active_] {
              if (!active->load(std::memory_order_acquire))
                return;
              std::lock_guard lock(owner->mu_);
              if (owner->movie_audio_track_)
                owner->movie_audio_track_->set_enabled(true);
              owner->attach_movie_audio_sink();
            },
            webrtc::TimeDelta::Millis(100));
      } else if (state ==
                 webrtc::PeerConnectionInterface::PeerConnectionState::kFailed)
        owner_.fail("PeerConnection failed");
    }
    void OnTrack(
        webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> value) override {
      owner_.on_track(std::move(value));
    }

  private:
    Impl &owner_;
  };
  bool setup() {
    observer_ = std::make_unique<PeerObserver>(*this);
    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    auto created = runtime_->factory()->CreatePeerConnectionOrError(
        config, webrtc::PeerConnectionDependencies(observer_.get()));
    if (!created.ok()) {
      fail("PeerConnection creation failed");
      return false;
    }
    peer_ = std::move(created.value());
    peer_->SetAudioPlayout(false);
    video_track_ =
        runtime_->factory()->CreateVideoTrack(video_source_, "movie-video");
    const auto policy = signaled_audio_policy(config_.audio_mode);
    const auto audio_kind = policy.processing_enabled
                                ? AudioSourceKind::microphone
                                : AudioSourceKind::synthetic;
    audio_source_ =
        runtime_->factory()->CreateAudioSource(audio_options(audio_kind));
    audio_track_ = runtime_->factory()->CreateAudioTrack(
        role_ == SignaledRole::host ? "host-voice" : "viewer-voice",
        audio_source_.get());
    if (!video_track_ || !audio_track_ ||
        !peer_->AddTrack(video_track_, {"shareme-test"}).ok() ||
        !peer_->AddTrack(audio_track_, {"shareme-test"}).ok()) {
      fail("adding test tracks failed");
      return false;
    }
    if (movie_audio_source_) {
      movie_audio_track_ = runtime_->factory()->CreateAudioTrack(
          "movie-audio", movie_audio_source_.get());
      if (!movie_audio_track_ || !movie_audio_track_->set_enabled(false) ||
          !peer_->AddTrack(movie_audio_track_, {"shareme-test"}).ok()) {
        fail("movie-audio-source-unavailable");
        return false;
      }
    }
    if (role_ == SignaledRole::host) {
      webrtc::DataChannelInit init;
      init.ordered = true;
      auto created = peer_->CreateDataChannelOrError(
          std::string(kControlChannelLabel), &init);
      if (!created.ok()) {
        fail("control-channel-creation-failed");
        return false;
      }
      attach_control_channel(std::move(created.value()));
    }
    return true;
  }
  void on_data_channel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) {
    if (role_ != SignaledRole::viewer || !channel ||
        !valid_control_channel(channel->label(), channel->ordered(),
                               channel->reliable()))
      return;
    attach_control_channel(std::move(channel));
  }
  void attach_control_channel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) {
    if (!channel || control_channel_)
      return;
    control_channel_ = std::move(channel);
    control_observer_ = std::make_unique<ControlObserver>(*this);
    control_channel_->RegisterObserver(control_observer_.get());
  }
  void create_offer() { create_description(true); }
  void create_answer() { create_description(false); }
  void create_description(bool offer) {
    auto observer = webrtc::scoped_refptr<CreateObserver>(new CreateObserver(
        [this, offer, active = callbacks_active_](auto desc) {
          if (active->load(std::memory_order_acquire))
            on_local_description(offer, std::move(desc));
        },
        [this, active = callbacks_active_](auto error) {
          if (active->load(std::memory_order_acquire))
            fail("description creation failed: " + error);
        }));
    if (offer)
      peer_->CreateOffer(observer.get(), {});
    else
      peer_->CreateAnswer(observer.get(), {});
  }
  void on_local_description(
      bool offer, std::unique_ptr<webrtc::SessionDescriptionInterface> desc) {
    const auto needs_movie_stereo =
        (offer && movie_audio_source_) ||
        (!offer && remote_movie_audio_mid_.has_value());
    const auto target_mid =
        offer ? std::optional<std::string>{} : remote_movie_audio_mid_;
    if (needs_movie_stereo &&
        !enable_movie_audio_stereo(*desc->description(), target_mid)) {
      fail("movie-audio-source-unavailable");
      return;
    }
    std::string sdp;
    if (!desc->ToString(&sdp)) {
      fail("SDP serialization failed");
      return;
    }
    auto observer = webrtc::scoped_refptr<SetObserver>(new SetObserver(
        [this, offer, sdp = std::move(sdp),
         active = callbacks_active_]() mutable {
          if (active->load(std::memory_order_acquire) && callbacks_.description)
            callbacks_.description(offer ? "offer" : "answer", std::move(sdp));
        },
        [this, active = callbacks_active_](auto error) {
          if (active->load(std::memory_order_acquire))
            fail("local description failed: " + error);
        }));
    peer_->SetLocalDescription(observer.get(), desc.release());
  }
  [[nodiscard]] static bool
  enable_movie_audio_stereo(webrtc::SessionDescription &description,
                            const std::optional<std::string> &target_mid) {
    for (auto &content : description.contents()) {
      if (target_mid && content.mid() != *target_mid)
        continue;
      auto *media = content.media_description();
      if (!media || media->type() != webrtc::MediaType::AUDIO)
        continue;
      if (!target_mid) {
        bool is_movie_audio = false;
        for (const auto &stream : media->streams()) {
          if (stream.id == "movie-audio") {
            is_movie_audio = true;
            break;
          }
        }
        if (!is_movie_audio)
          continue;
      }
      auto codecs = media->codecs();
      bool opus_found = false;
      for (auto &codec : codecs) {
        if (codec.name != "opus" && codec.name != "OPUS")
          continue;
        codec.params["stereo"] = "1";
        codec.params["sprop-stereo"] = "1";
        opus_found = true;
      }
      if (opus_found)
        media->set_codecs(codecs);
      return opus_found;
    }
    return false;
  }
  [[nodiscard]] static std::optional<std::string>
  find_track_mid(const webrtc::SessionDescription &description,
                 std::string_view track_id) {
    for (const auto &content : description.contents()) {
      const auto *media = content.media_description();
      if (!media || media->type() != webrtc::MediaType::AUDIO)
        continue;
      for (const auto &stream : media->streams())
        if (stream.id == track_id)
          return content.mid();
    }
    return std::nullopt;
  }
  void apply_remote(std::string type, std::string sdp) {
    const auto sdp_type =
        type == "offer" ? webrtc::SdpType::kOffer : webrtc::SdpType::kAnswer;
    auto desc = webrtc::CreateSessionDescription(sdp_type, sdp);
    if (!desc) {
      fail("remote SDP parsing failed");
      return;
    }
    const auto remote_voice_id =
        role_ == SignaledRole::host ? "viewer-voice" : "host-voice";
    const auto remote_voice_mid =
        find_track_mid(*desc->description(), remote_voice_id);
    {
      std::lock_guard lock(mu_);
      remote_voice_mid_ = remote_voice_mid;
    }
    if (type == "offer" && role_ == SignaledRole::viewer) {
      remote_movie_audio_mid_ =
          find_track_mid(*desc->description(), "movie-audio");
      expected_remote_movie_audio_.store(remote_movie_audio_mid_.has_value(),
                                         std::memory_order_release);
    }
    auto observer = webrtc::scoped_refptr<SetObserver>(new SetObserver(
        [this, active = callbacks_active_] {
          if (!active->load(std::memory_order_acquire))
            return;
          remote_set_ = true;
          for (auto &candidate : candidates_.drain())
            add_candidate(std::move(candidate));
          if (role_ == SignaledRole::viewer)
            create_answer();
        },
        [this, active = callbacks_active_](auto error) {
          if (active->load(std::memory_order_acquire))
            fail("remote description failed: " + error);
        }));
    peer_->SetRemoteDescription(observer.get(), desc.release());
  }
  void add_candidate(PendingCandidate value) {
    webrtc::SdpParseError error;
    auto candidate = webrtc::IceCandidate::Create(value.mid, value.line,
                                                  value.candidate, &error);
    if (!candidate || !peer_->AddIceCandidate(candidate.get()))
      fail("remote ICE candidate rejected");
  }
  void on_track(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> value) {
    if (!value || !value->receiver() || !value->receiver()->track())
      return;
    auto track = value->receiver()->track();
    if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
      remote_video_ = static_cast<webrtc::VideoTrackInterface *>(track.get());
      remote_video_->AddOrUpdateSink(&sink_, {});
    } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
      auto audio_track = webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
          static_cast<webrtc::AudioTrackInterface *>(track.get()));
      if (track->id() == "movie-audio") {
        if (remote_movie_audio_ && movie_audio_sink_attached_)
          remote_movie_audio_->RemoveSink(&movie_audio_sink_);
        remote_movie_audio_ = std::move(audio_track);
        movie_audio_sink_attached_ = false;
        remote_movie_audio_seen_.store(true, std::memory_order_release);
        std::lock_guard lock(mu_);
        if (result_.connected)
          attach_movie_audio_sink();
      } else {
        remote_audio_ = std::move(audio_track);
        remote_audio_->set_enabled(false);
      }
    }
  }
  void populate_media_result() {
    result_.video_frames_received = sink_.frame_count();
    result_.video_width = sink_.last_width();
    result_.video_height = sink_.last_height();
    const auto movie_audio = movie_audio_sink_.snapshot();
    result_.movie_audio_frames_received = movie_audio.valid_callback_count;
    result_.movie_audio_invalid_frames_received =
        movie_audio.invalid_callback_count;
    result_.movie_audio_sample_rate = movie_audio.sample_rate;
    result_.movie_audio_channels = movie_audio.channels;
    result_.movie_audio_peak = movie_audio.peak;
    if (!movie_audio_source_)
      return;
    result_.movie_audio_chunks_generated =
        movie_audio_source_->generated_count();
    const auto audio_pts = movie_audio_source_->last_pts_ms();
    const auto video_pts = video_source_->last_pts_ms();
    if (audio_pts && video_pts)
      result_.movie_av_skew_ms =
          checked_absolute_difference(*audio_pts, *video_pts);
  }
  void attach_movie_audio_sink() {
    if (remote_movie_audio_ && !movie_audio_sink_attached_) {
      remote_movie_audio_->AddSink(&movie_audio_sink_);
      movie_audio_sink_attached_ = true;
    }
  }
  void stop_movie_audio_source() noexcept {
    if (movie_audio_source_ && !movie_audio_source_stopped_.exchange(
                                   true, std::memory_order_acq_rel)) {
      movie_audio_source_->stop();
    }
  }
  void collect_stats() {
    if (!runtime_ || !peer_)
      return;
    auto callback = webrtc::scoped_refptr<StatsObserver>(new StatsObserver());
    runtime_->signaling_thread()->BlockingCall(
        [&] { peer_->GetStats(callback.get()); });
    auto report = callback->wait();
    if (!report)
      return;
    std::lock_guard lock(mu_);
    for (const auto *stats :
         report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
      const auto *source =
          stats->media_source_id
              ? report->GetAs<webrtc::RTCAudioSourceStats>(
                    *stats->media_source_id)
              : nullptr;
      if (stats->kind == std::optional<std::string>{"audio"} && source &&
          source->track_identifier &&
          is_expected_voice_rtp_track(role_, true, *source->track_identifier))
        result_.audio_packets_sent += stats->packets_sent.value_or(0);
    }
    for (const auto *stats :
         report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>())
      if (stats->kind == std::optional<std::string>{"audio"} &&
          is_expected_inbound_voice_rtp_track(
              role_, stats->track_identifier.value_or(""),
              stats->mid.value_or(""), remote_voice_mid_.value_or("")))
        result_.audio_packets_received += stats->packets_received.value_or(0);
    for (const auto *source :
         report->GetStatsOfType<webrtc::RTCAudioSourceStats>())
      if (source->audio_level && source->track_identifier &&
          is_expected_voice_rtp_track(role_, true, *source->track_identifier))
        result_.local_audio_level = *source->audio_level;
    for (const auto *transport :
         report->GetStatsOfType<webrtc::RTCTransportStats>()) {
      if (!transport->selected_candidate_pair_id)
        continue;
      const auto *pair = report->GetAs<webrtc::RTCIceCandidatePairStats>(
          *transport->selected_candidate_pair_id);
      if (!pair || !pair->local_candidate_id)
        continue;
      const auto *candidate = report->GetAs<webrtc::RTCLocalIceCandidateStats>(
          *pair->local_candidate_id);
      if (candidate && candidate->candidate_type)
        result_.selected_candidate_type = *candidate->candidate_type;
    }
  }
  void fail(std::string error) {
    std::string category;
    {
      std::lock_guard lock(mu_);
      if (!result_.error.empty())
        return;
      result_.error = std::move(error);
      category = result_.error;
    }
    if (callbacks_.failure)
      callbacks_.failure(std::move(category));
  }
  SignaledPeerConfig config_;
  SignaledRole role_;
  SignaledPeerCallbacks callbacks_;
  std::shared_ptr<WebRtcRuntime> runtime_;
  std::unique_ptr<webrtc::TaskQueueFactory> queues_;
  webrtc::scoped_refptr<LocalVideoSource> video_source_;
  webrtc::scoped_refptr<LocalAudioSource> movie_audio_source_;
  RemoteVideoSink sink_;
  CountingAudioSink movie_audio_sink_;
  std::unique_ptr<PeerObserver> observer_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_,
      remote_video_;
  webrtc::scoped_refptr<webrtc::AudioSourceInterface> audio_source_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_,
      remote_audio_, movie_audio_track_, remote_movie_audio_;
  webrtc::scoped_refptr<webrtc::DataChannelInterface> control_channel_;
  std::unique_ptr<ControlObserver> control_observer_;
  CandidateStager<PendingCandidate, 64> candidates_;
  bool remote_set_{};
  bool movie_audio_sink_attached_{};
  std::mutex mu_;
  SignaledPeerResult result_;
  std::atomic_bool wait_cancelled_{false};
  std::atomic_bool stopped_{false};
  std::atomic_bool expected_remote_movie_audio_{false};
  std::atomic_bool remote_movie_audio_seen_{false};
  std::atomic_bool movie_audio_source_stopped_{false};
  std::optional<std::string> remote_voice_mid_;
  std::optional<std::string> remote_movie_audio_mid_;
  std::shared_ptr<std::atomic_bool> callbacks_active_{
      std::make_shared<std::atomic_bool>(true)};
};

std::unique_ptr<SignaledPeer>
SignaledPeer::create(SignaledPeerConfig config,
                     SignaledPeerCallbacks callbacks) {
  auto impl = std::make_unique<Impl>(config, std::move(callbacks));
  if (!impl->initialize())
    return nullptr;
  return std::unique_ptr<SignaledPeer>(new SignaledPeer(std::move(impl)));
}
std::unique_ptr<SignaledPeer>
SignaledPeer::create(SignaledRole role, SignaledPeerCallbacks callbacks) {
  return create({.role = role}, std::move(callbacks));
}
SignaledPeer::SignaledPeer(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
SignaledPeer::~SignaledPeer() = default;
bool SignaledPeer::start() { return impl_->start(); }
bool SignaledPeer::receive_description(std::string type, std::string sdp) {
  return impl_->receive_description(std::move(type), std::move(sdp));
}
bool SignaledPeer::receive_candidate(std::string mid, int line,
                                     std::string candidate) {
  return impl_->receive_candidate(std::move(mid), line, std::move(candidate));
}
bool SignaledPeer::send_control_message(std::string message) {
  return impl_->send_control_message(std::move(message));
}
SignaledPeerResult SignaledPeer::wait(std::chrono::milliseconds timeout) {
  return impl_->wait(timeout);
}
void SignaledPeer::cancel_wait() noexcept { impl_->cancel_wait(); }
void SignaledPeer::stop() noexcept { impl_->stop(); }
} // namespace shareme::rtc
