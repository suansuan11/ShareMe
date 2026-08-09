#include "shareme/rtc/signaled_peer.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "api/environment/environment_factory.h"
#include "api/data_channel_interface.h"
#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/ref_counted_base.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "audio_device_factory.hpp"
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

struct ControlMessageState {
  explicit ControlMessageState(std::function<void(bool)> completion)
      : completion(std::move(completion)) {}

  void complete(bool sent) {
    if (!completed.exchange(true, std::memory_order_acq_rel) && completion)
      completion(sent);
  }

  std::function<void(bool)> completion;
  std::atomic_bool completed{false};
};

struct ControlChannelState {
  std::atomic_bool active{true};
  std::atomic_bool open{false};
  webrtc::scoped_refptr<webrtc::DataChannelInterface> channel;
  std::mutex pending_mutex;
  std::vector<std::shared_ptr<ControlMessageState>> pending;
};

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
  const auto valid_direction =
      config.video_direction == SignaledVideoDirection::send_receive ||
      config.video_direction == SignaledVideoDirection::send_only ||
      config.video_direction == SignaledVideoDirection::receive_only;
  const auto sends_video =
      config.video_direction != SignaledVideoDirection::receive_only;
  const auto valid_video =
      !sends_video || config.video_mode == SignaledVideoMode::synthetic ||
      (config.video_mode == SignaledVideoMode::injected &&
       static_cast<bool>(config.video_source_factory));
  return valid_role && valid_audio && valid_direction && valid_video;
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
        local_video_sink_(config_.local_video_frame),
        remote_video_sink_(config_.remote_video_frame) {}
  bool initialize() {
    if (!valid_signaled_peer_config(config_)) {
      if (config_.role != SignaledRole::host &&
          config_.role != SignaledRole::viewer)
        fail("invalid-role");
      else if (config_.audio_mode != SignaledAudioMode::synthetic &&
               config_.audio_mode != SignaledAudioMode::microphone)
        fail("invalid-audio-mode");
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
    runtime_ = WebRtcRuntime::create(audio.device,
                                     std::move(config_.video_encoder_factory));
    if (!runtime_) {
      fail("WebRTC runtime creation failed");
      return false;
    }
    queues_ = webrtc::CreateDefaultTaskQueueFactory();
    if (config_.video_direction != SignaledVideoDirection::receive_only) {
      if (config_.video_mode == SignaledVideoMode::injected)
        video_source_ = config_.video_source_factory(*queues_);
      else
        video_source_ = TestPatternSource::create(*queues_, 640, 360, 30);
      if (!video_source_) {
        fail("video-source-unavailable");
        return false;
      }
    }
    return runtime_->signaling_thread()->BlockingCall(
        [this] { return setup(); });
  }
  bool start() {
    if (!runtime_ || !peer_)
      return false;
    if (video_source_ && !video_source_->start()) {
      const auto source_error = video_source_->error();
      fail(source_error.empty() ? "video-source-start-failed" : source_error);
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
    if (!valid_control_message(message) || !runtime_ ||
        !runtime_->signaling_thread() ||
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
  bool queue_control_message(std::string message,
                             std::function<void(bool)> completion) {
    if (!valid_control_message(message) || !runtime_ ||
        !runtime_->signaling_thread() ||
        !runtime_->signaling_thread()->RunningForTest() ||
        !control_channel_state_->open.load(std::memory_order_acquire))
      return false;
    const auto state = control_channel_state_;
    const auto message_state = std::make_shared<ControlMessageState>(
        std::move(completion));
    {
      std::lock_guard lock(state->pending_mutex);
      if (!state->active.load(std::memory_order_acquire) ||
          !state->open.load(std::memory_order_acquire))
        return false;
      state->pending.push_back(message_state);
    }
    try {
      runtime_->signaling_thread()->PostTask(
          [state, message = std::move(message), message_state] {
            bool sent = false;
            {
              std::lock_guard lock(state->pending_mutex);
              if (!state->active.load(std::memory_order_acquire) ||
                  !state->channel ||
                  state->channel->state() !=
                      webrtc::DataChannelInterface::kOpen) {
                state->open.store(false, std::memory_order_release);
              } else {
                sent = state->channel->Send(webrtc::DataBuffer(message));
                if (!sent)
                  state->open.store(false, std::memory_order_release);
              }
              std::erase(state->pending, message_state);
            }
            message_state->complete(sent);
          });
    } catch (...) {
      {
        std::lock_guard lock(state->pending_mutex);
        std::erase(state->pending, message_state);
      }
      message_state->complete(false);
      return false;
    }
    return true;
  }
  SignaledMediaStats media_stats() const noexcept {
    SignaledMediaStats result;
    try {
      if (!runtime_ || !peer_ || !runtime_->signaling_thread() ||
          !runtime_->signaling_thread()->RunningForTest()) {
        result.unavailable = true;
        return result;
      }

      auto callback = webrtc::scoped_refptr<StatsObserver>(new StatsObserver());
      const auto peer = peer_;
      runtime_->signaling_thread()->PostTask(
          [peer, callback] { peer->GetStats(callback.get()); });
      const auto report = callback->wait();
      if (!report) {
        result.unavailable = true;
        return result;
      }

      bool found_video_stats = false;
      bool missing_video_field = false;
      bool found_outbound_voice_stats = false;
      bool found_inbound_voice_stats = false;
      bool missing_voice_field = false;
      std::string expected_voice_mid;
      {
        std::lock_guard lock(mu_);
        expected_voice_mid = remote_voice_mid_.value_or("");
      }
      for (const auto* const stats :
           report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
        if (stats->kind == std::optional<std::string>{"audio"}) {
          const auto *source =
              stats->media_source_id
                  ? report->GetAs<webrtc::RTCAudioSourceStats>(
                        *stats->media_source_id)
                  : nullptr;
          if (source && source->track_identifier &&
              is_expected_voice_rtp_track(role_, true,
                                          *source->track_identifier)) {
            found_outbound_voice_stats = true;
            if (stats->packets_sent) {
              result.voice_packets_sent =
                  result.voice_packets_sent.value_or(0) + *stats->packets_sent;
            } else {
              missing_voice_field = true;
            }
            if (stats->bytes_sent) {
              result.voice_bytes_sent =
                  result.voice_bytes_sent.value_or(0) + *stats->bytes_sent;
            } else {
              missing_voice_field = true;
            }
          }
          continue;
        }
        if (stats->kind != std::optional<std::string>{"video"}) {
          continue;
        }
        found_video_stats = true;
        if (stats->frames_encoded) {
          result.frames_encoded = *stats->frames_encoded;
        } else {
          missing_video_field = true;
        }
        if (stats->frames_sent) {
          result.frames_sent = *stats->frames_sent;
        } else {
          missing_video_field = true;
        }
        if (stats->bytes_sent)
          result.bytes_sent = *stats->bytes_sent;
      }
      for (const auto* const stats :
           report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
        if (stats->kind == std::optional<std::string>{"audio"}) {
          if (is_expected_inbound_voice_rtp_track(
                  role_, stats->track_identifier.value_or(""),
                  stats->mid.value_or(""), expected_voice_mid)) {
            found_inbound_voice_stats = true;
            if (stats->packets_received) {
              result.voice_packets_received =
                  result.voice_packets_received.value_or(0) +
                  *stats->packets_received;
            } else {
              missing_voice_field = true;
            }
            if (stats->bytes_received) {
              result.voice_bytes_received =
                  result.voice_bytes_received.value_or(0) +
                  *stats->bytes_received;
            } else {
              missing_voice_field = true;
            }
          }
          continue;
        }
        if (stats->kind != std::optional<std::string>{"video"}) {
          continue;
        }
        found_video_stats = true;
        if (stats->frames_received) {
          result.frames_received = *stats->frames_received;
        } else {
          missing_video_field = true;
        }
        if (stats->frames_decoded) {
          result.frames_decoded = *stats->frames_decoded;
        } else {
          missing_video_field = true;
        }
        if (stats->frames_dropped) {
          result.frames_dropped = *stats->frames_dropped;
        } else {
          missing_video_field = true;
        }
        if (stats->bytes_received)
          result.bytes_received = *stats->bytes_received;
      }
      result.unavailable = !found_video_stats || missing_video_field ||
                           !found_outbound_voice_stats ||
                           !found_inbound_voice_stats || missing_voice_field;
    } catch (...) {
      result = {};
      result.unavailable = true;
    }
    return result;
  }
  std::string video_source_error() const {
    return video_source_ ? video_source_->error() : std::string{};
  }
  SignaledPeerResult wait(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::optional<std::chrono::steady_clock::time_point> media_ready;
    while (std::chrono::steady_clock::now() < deadline) {
      if (wait_cancelled_.load(std::memory_order_acquire))
        break;
      const auto source_error = video_source_ ? video_source_->error() : "";
      if (!source_error.empty()) {
        fail(source_error);
        break;
      }
      {
        std::lock_guard lock(mu_);
        if (!result_.error.empty())
          break;
        const auto remote_video_ready =
            config_.video_direction == SignaledVideoDirection::send_only ||
            remote_video_sink_.frame_count() > 0;
        if (result_.connected && remote_video_ready && !media_ready)
          media_ready = std::chrono::steady_clock::now();
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
    if (!result_.connected && result_.error.empty())
      result_.error = "signaled call timed out";
    if (config_.video_direction != SignaledVideoDirection::send_only &&
        result_.video_frames_received == 0 && result_.error.empty())
      result_.error = "no remote video received";
    if ((result_.audio_packets_sent == 0 ||
         result_.audio_packets_received == 0) &&
        result_.error.empty())
      result_.error = "no bidirectional audio RTP";
    return result_;
  }
  bool local_audio_enabled() const noexcept {
    return local_audio_enabled_.load(std::memory_order_acquire);
  }
  bool remote_audio_enabled() const noexcept {
    return remote_audio_enabled_.load(std::memory_order_acquire);
  }
  bool set_local_audio_enabled(bool enabled) noexcept {
    if (stopped_.load(std::memory_order_acquire) || !runtime_ ||
        !runtime_->signaling_thread())
      return false;
    bool applied = false;
    try {
      runtime_->signaling_thread()->BlockingCall([&] {
        if (!stopped_.load(std::memory_order_acquire) && audio_track_) {
          audio_track_->set_enabled(enabled);
          applied = true;
        }
      });
    } catch (...) {
      return false;
    }
    if (applied)
      local_audio_enabled_.store(enabled, std::memory_order_release);
    return applied;
  }
  bool set_remote_audio_enabled(bool enabled) noexcept {
    if (stopped_.load(std::memory_order_acquire) || !runtime_ ||
        !runtime_->signaling_thread())
      return false;
    bool applied = false;
    try {
      runtime_->signaling_thread()->BlockingCall([&] {
        if (!stopped_.load(std::memory_order_acquire) && peer_) {
          peer_->SetAudioPlayout(enabled);
          if (remote_audio_)
            remote_audio_->set_enabled(enabled);
          applied = true;
        }
      });
    } catch (...) {
      return false;
    }
    if (applied)
      remote_audio_enabled_.store(enabled, std::memory_order_release);
    return applied;
  }
  void cancel_wait() noexcept {
    wait_cancelled_.store(true, std::memory_order_release);
  }
  void stop() {
    if (stopped_.exchange(true))
      return;
    callbacks_active_->store(false, std::memory_order_release);
    control_channel_state_->active.store(false, std::memory_order_release);
    control_channel_state_->open.store(false, std::memory_order_release);
    std::vector<std::shared_ptr<ControlMessageState>> pending_messages;
    {
      std::lock_guard lock(control_channel_state_->pending_mutex);
      pending_messages.swap(control_channel_state_->pending);
    }
    for (const auto &message : pending_messages)
      message->complete(false);
    local_video_sink_.clear_callback();
    remote_video_sink_.clear_callback();
    if (video_source_)
      video_source_->stop();
    if (runtime_ && runtime_->signaling_thread() &&
        runtime_->signaling_thread()->RunningForTest())
      runtime_->signaling_thread()->BlockingCall([this] {
        if (video_track_)
          video_track_->RemoveSink(&local_video_sink_);
        if (remote_video_)
          remote_video_->RemoveSink(&remote_video_sink_);
        if (control_channel_ && control_observer_)
          control_channel_->UnregisterObserver();
        control_observer_.reset();
        {
          std::lock_guard lock(control_channel_state_->pending_mutex);
          control_channel_state_->channel = nullptr;
        }
        control_channel_ = nullptr;
        peer_ = nullptr;
        remote_video_ = nullptr;
        remote_audio_ = nullptr;
        video_track_ = nullptr;
        audio_track_ = nullptr;
        audio_source_ = nullptr;
        observer_.reset();
      });
    video_source_ = nullptr;
    if (runtime_)
      static_cast<void>(runtime_->stop());
  }
  ~Impl() { stop(); }

private:
  class ControlObserver final : public webrtc::DataChannelObserver {
  public:
    explicit ControlObserver(Impl &owner) : owner_(owner) {}
    void OnStateChange() override {
      owner_.control_channel_state_->open.store(
          owner_.control_channel_ &&
              owner_.control_channel_->state() ==
                  webrtc::DataChannelInterface::kOpen,
          std::memory_order_release);
    }
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
    peer_->SetAudioPlayout(config_.native_audio_playout);
    if (video_source_) {
      video_track_ =
          runtime_->factory()->CreateVideoTrack(video_source_, "movie-video");
      if (video_track_)
        video_track_->AddOrUpdateSink(&local_video_sink_, {});
    }
    const auto policy = signaled_audio_policy(config_.audio_mode);
    const auto audio_kind = policy.processing_enabled
                                ? AudioSourceKind::microphone
                                : AudioSourceKind::synthetic;
    audio_source_ =
        runtime_->factory()->CreateAudioSource(audio_options(audio_kind));
    audio_track_ = runtime_->factory()->CreateAudioTrack(
        role_ == SignaledRole::host ? "host-voice" : "viewer-voice",
        audio_source_.get());
    if (!audio_track_) {
      fail("adding test tracks failed");
      return false;
    }
    webrtc::RtpTransceiverInit video_init;
    if (video_track_)
      video_init.stream_ids = {"shareme-test"};
    video_init.direction =
        config_.video_direction == SignaledVideoDirection::send_only
            ? webrtc::RtpTransceiverDirection::kSendOnly
        : config_.video_direction == SignaledVideoDirection::receive_only
            ? webrtc::RtpTransceiverDirection::kRecvOnly
            : webrtc::RtpTransceiverDirection::kSendRecv;
    auto video_transceiver = video_track_
                                 ? peer_->AddTransceiver(video_track_, video_init)
                                 : peer_->AddTransceiver(webrtc::MediaType::VIDEO,
                                                         video_init);
    auto audio_sender = peer_->AddTrack(audio_track_, {"shareme-test"});
    if (!video_transceiver.ok() || !audio_sender.ok()) {
      fail("adding test tracks failed");
      return false;
    }
    if (config_.preserve_video_quality && video_track_) {
      auto parameters = video_transceiver.value()->sender()->GetParameters();
      parameters.degradation_preference =
          webrtc::DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION;
      const auto error =
          video_transceiver.value()->sender()->SetParameters(parameters);
      if (!error.ok()) {
        fail("preserving video quality failed: " +
             std::string(error.message()));
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
    {
      std::lock_guard lock(control_channel_state_->pending_mutex);
      control_channel_state_->channel = control_channel_;
    }
    control_channel_state_->open.store(
        control_channel_->state() == webrtc::DataChannelInterface::kOpen,
        std::memory_order_release);
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
      remote_video_->AddOrUpdateSink(&remote_video_sink_, {});
    } else if (track->kind() == webrtc::MediaStreamTrackInterface::kAudioKind) {
      auto audio_track = webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
          static_cast<webrtc::AudioTrackInterface *>(track.get()));
      remote_audio_ = std::move(audio_track);
      remote_audio_->set_enabled(
          remote_audio_enabled_.load(std::memory_order_acquire));
    }
  }
  void populate_media_result() {
    result_.video_frames_received = remote_video_sink_.frame_count();
    result_.video_width = remote_video_sink_.last_width();
    result_.video_height = remote_video_sink_.last_height();
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
  RemoteVideoSink local_video_sink_;
  RemoteVideoSink remote_video_sink_;
  std::unique_ptr<PeerObserver> observer_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_,
      remote_video_;
  webrtc::scoped_refptr<webrtc::AudioSourceInterface> audio_source_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_,
      remote_audio_;
  webrtc::scoped_refptr<webrtc::DataChannelInterface> control_channel_;
  std::shared_ptr<ControlChannelState> control_channel_state_{
      std::make_shared<ControlChannelState>()};
  std::unique_ptr<ControlObserver> control_observer_;
  CandidateStager<PendingCandidate, 64> candidates_;
  bool remote_set_{};
  mutable std::mutex mu_;
  SignaledPeerResult result_;
  std::atomic_bool wait_cancelled_{false};
  std::atomic_bool stopped_{false};
  std::atomic_bool local_audio_enabled_{true};
  std::atomic_bool remote_audio_enabled_{config_.native_audio_playout};
  std::optional<std::string> remote_voice_mid_;
  std::shared_ptr<std::atomic_bool> callbacks_active_{
      std::make_shared<std::atomic_bool>(true)};
};

VideoCodecReport SignaledPeer::video_codec_report() noexcept {
  return {"vp8-software", "unavailable-locked-abi"};
}

std::unique_ptr<SignaledPeer>
SignaledPeer::create(SignaledPeerConfig config,
                     SignaledPeerCallbacks callbacks) {
  auto impl = std::make_unique<Impl>(std::move(config), std::move(callbacks));
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
bool SignaledPeer::queue_control_message(
    std::string message, std::function<void(bool)> completion) {
  return impl_->queue_control_message(std::move(message),
                                      std::move(completion));
}
SignaledPeerResult SignaledPeer::wait(std::chrono::milliseconds timeout) {
  return impl_->wait(timeout);
}
SignaledMediaStats SignaledPeer::media_stats() const noexcept {
  return impl_->media_stats();
}
std::string SignaledPeer::video_source_error() const {
  return impl_->video_source_error();
}
bool SignaledPeer::local_audio_enabled() const noexcept {
  return impl_->local_audio_enabled();
}
bool SignaledPeer::remote_audio_enabled() const noexcept {
  return impl_->remote_audio_enabled();
}
bool SignaledPeer::set_local_audio_enabled(bool enabled) noexcept {
  return impl_->set_local_audio_enabled(enabled);
}
bool SignaledPeer::set_remote_audio_enabled(bool enabled) noexcept {
  return impl_->set_remote_audio_enabled(enabled);
}
void SignaledPeer::cancel_wait() noexcept { impl_->cancel_wait(); }
void SignaledPeer::stop() noexcept { impl_->stop(); }
} // namespace shareme::rtc
