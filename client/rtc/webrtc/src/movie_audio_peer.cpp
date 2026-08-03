#include "shareme/rtc/movie_audio_peer.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/ref_counted_base.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/units/time_delta.h"
#include "audio_device_factory.hpp"
#include "counting_audio_sink.hpp"
#include "pc/session_description.h"
#include "rtc_base/thread.h"
#include "shareme/rtc/candidate_stager.hpp"
#include "webrtc_runtime.hpp"

namespace shareme::rtc {
namespace {

struct PendingCandidate {
  std::string mid;
  int line{};
  std::string candidate;
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
  [[nodiscard]] webrtc::scoped_refptr<const webrtc::RTCStatsReport> wait() {
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

[[nodiscard]] bool valid_config(const MovieAudioPeerConfig &config) noexcept {
  if (config.role == SignaledRole::host)
    return static_cast<bool>(config.source_factory);
  return config.role == SignaledRole::viewer && !config.source_factory;
}

} // namespace

class MovieAudioPeer::Impl final {
public:
  Impl(MovieAudioPeerConfig config, MovieAudioPeerCallbacks callbacks)
      : config_(std::move(config)), callbacks_(std::move(callbacks)) {}
  ~Impl() { stop(); }

  bool initialize() {
    if (!valid_config(config_))
      return false;
    auto audio = create_audio_device(webrtc::CreateEnvironment(),
                                     AudioDeviceMode::synthetic);
    if (!audio.ok()) {
      fail("audio-device-unavailable");
      return false;
    }
    runtime_ = WebRtcRuntime::create(audio.device);
    if (!runtime_) {
      fail("webrtc-runtime-unavailable");
      return false;
    }
    if (config_.role == SignaledRole::host) {
      source_ = config_.source_factory();
      if (!source_) {
        fail("movie-audio-source-unavailable");
        return false;
      }
    }
    return runtime_->signaling_thread()->BlockingCall(
        [this] { return setup(); });
  }

  bool start() {
    if (!runtime_ || !peer_)
      return false;
    if (source_ && !source_->start()) {
      fail(source_->error().empty() ? "movie-audio-source-start-failed"
                                    : "movie-audio-source-unavailable");
      stop_source();
      return false;
    }
    if (config_.role == SignaledRole::host)
      runtime_->signaling_thread()->PostTask([this] { create_description(true); });
    return true;
  }

  bool receive_description(std::string type, std::string sdp) {
    if (!runtime_ || !valid_remote_description(config_.role, type, sdp))
      return false;
    runtime_->signaling_thread()->PostTask(
        [this, type = std::move(type), sdp = std::move(sdp)]() mutable {
          apply_remote(std::move(type), std::move(sdp));
        });
    return true;
  }

  bool receive_candidate(std::string mid, int line, std::string candidate) {
    if (!runtime_ || !valid_remote_candidate(mid, line, candidate))
      return false;
    runtime_->signaling_thread()->PostTask(
        [this, value = PendingCandidate{std::move(mid), line,
                                        std::move(candidate)}]() mutable {
          if (remote_set_)
            add_candidate(std::move(value));
          else if (!candidates_.stage(std::move(value)))
            fail("pending-ice-candidate-limit-exceeded");
        });
    return true;
  }

  MovieAudioPeerResult wait(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline &&
           !wait_cancelled_.load(std::memory_order_acquire)) {
      const auto snapshot = sink_.snapshot();
      {
        std::lock_guard lock(mu_);
        if (!result_.error.empty() ||
            (result_.connected && snapshot.valid_callback_count >= 100))
          break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    collect_stats();
    std::lock_guard lock(mu_);
    populate_result();
    if (wait_cancelled_.load(std::memory_order_acquire) && result_.error.empty())
      result_.error = "movie-audio-wait-cancelled";
    if (!result_.connected && result_.error.empty())
      result_.error = "movie-audio-connection-timed-out";
    if (result_.connected && result_.frames_received < 100 &&
        result_.error.empty())
      result_.error = "movie-audio-unavailable";
    return result_;
  }

  void cancel_wait() noexcept {
    wait_cancelled_.store(true, std::memory_order_release);
  }

  void stop() noexcept {
    if (stopped_.exchange(true))
      return;
    cancel_wait();
    callbacks_active_->store(false, std::memory_order_release);
    stop_source();
    if (runtime_ && runtime_->signaling_thread() &&
        runtime_->signaling_thread()->RunningForTest()) {
      runtime_->signaling_thread()->BlockingCall([this] {
        if (remote_track_ && sink_attached_)
          remote_track_->RemoveSink(&sink_);
        sink_attached_ = false;
        remote_track_ = nullptr;
        local_track_ = nullptr;
        peer_ = nullptr;
        observer_.reset();
      });
    }
  }

private:
  class PeerObserver final : public webrtc::PeerConnectionObserver {
  public:
    explicit PeerObserver(Impl &owner) : owner_(owner) {}
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
    void OnIceCandidate(const webrtc::IceCandidate *candidate) override {
      if (candidate && owner_.callbacks_active_->load(std::memory_order_acquire) &&
          owner_.callbacks_.candidate)
        owner_.callbacks_.candidate(candidate->sdp_mid(),
                                    candidate->sdp_mline_index(),
                                    candidate->ToString());
    }
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state) override {
      if (state == webrtc::PeerConnectionInterface::kIceConnectionFailed)
        owner_.fail("ice-connection-failed");
    }
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState state) override {
      if (state == webrtc::PeerConnectionInterface::PeerConnectionState::kConnected) {
        {
          std::lock_guard lock(owner_.mu_);
          owner_.result_.connected = true;
        }
        owner_.runtime_->signaling_thread()->PostDelayedTask(
            [owner = &owner_, active = owner_.callbacks_active_] {
              if (active->load(std::memory_order_acquire))
                owner->attach_sink();
            },
            webrtc::TimeDelta::Millis(100));
      } else if (state == webrtc::PeerConnectionInterface::PeerConnectionState::kFailed) {
        owner_.fail("peer-connection-failed");
      }
    }
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override {
      owner_.on_track(std::move(transceiver));
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
      fail("peer-connection-creation-failed");
      return false;
    }
    peer_ = std::move(created.value());
    peer_->SetAudioRecording(false);
    peer_->SetAudioPlayout(false);
    if (config_.role != SignaledRole::host)
      return true;
    local_track_ = runtime_->factory()->CreateAudioTrack("movie-audio", source_.get());
    if (!local_track_ || !peer_->AddTrack(local_track_, {"shareme-movie"}).ok()) {
      fail("movie-audio-track-creation-failed");
      return false;
    }
    return true;
  }

  void create_description(bool offer) {
    auto observer = webrtc::scoped_refptr<CreateObserver>(new CreateObserver(
        [this, offer, active = callbacks_active_](auto description) {
          if (active->load(std::memory_order_acquire))
            on_local_description(offer, std::move(description));
        },
        [this, active = callbacks_active_](std::string error) {
          if (active->load(std::memory_order_acquire))
            fail("description-creation-failed: " + error);
        }));
    if (offer)
      peer_->CreateOffer(observer.get(), {});
    else
      peer_->CreateAnswer(observer.get(), {});
  }

  void on_local_description(bool offer,
                            std::unique_ptr<webrtc::SessionDescriptionInterface> description) {
    if (!enable_stereo(*description->description())) {
      fail("movie-audio-stereo-unavailable");
      return;
    }
    std::string sdp;
    if (!description->ToString(&sdp)) {
      fail("sdp-serialization-failed");
      return;
    }
    auto observer = webrtc::scoped_refptr<SetObserver>(new SetObserver(
        [this, offer, sdp = std::move(sdp), active = callbacks_active_]() mutable {
          if (active->load(std::memory_order_acquire) && callbacks_.description)
            callbacks_.description(offer ? "offer" : "answer", std::move(sdp));
        },
        [this, active = callbacks_active_](std::string error) {
          if (active->load(std::memory_order_acquire))
            fail("local-description-failed: " + error);
        }));
    peer_->SetLocalDescription(observer.get(), description.release());
  }

  static bool enable_stereo(webrtc::SessionDescription &description) {
    for (auto &content : description.contents()) {
      auto *media = content.media_description();
      if (!media || media->type() != webrtc::MediaType::AUDIO)
        continue;
      auto codecs = media->codecs();
      bool opus_found = false;
      for (auto &codec : codecs) {
        if (codec.name == "opus" || codec.name == "OPUS") {
          codec.params["stereo"] = "1";
          codec.params["sprop-stereo"] = "1";
          opus_found = true;
        }
      }
      if (opus_found)
        media->set_codecs(codecs);
      return opus_found;
    }
    return false;
  }

  void apply_remote(std::string type, std::string sdp) {
    const auto sdp_type = type == "offer" ? webrtc::SdpType::kOffer : webrtc::SdpType::kAnswer;
    auto description = webrtc::CreateSessionDescription(sdp_type, sdp);
    if (!description) {
      fail("remote-sdp-parsing-failed");
      return;
    }
    auto observer = webrtc::scoped_refptr<SetObserver>(new SetObserver(
        [this, active = callbacks_active_] {
          if (!active->load(std::memory_order_acquire))
            return;
          remote_set_ = true;
          for (auto &candidate : candidates_.drain())
            add_candidate(std::move(candidate));
          if (config_.role == SignaledRole::viewer)
            create_description(false);
        },
        [this, active = callbacks_active_](std::string error) {
          if (active->load(std::memory_order_acquire))
            fail("remote-description-failed: " + error);
        }));
    peer_->SetRemoteDescription(observer.get(), description.release());
  }

  void add_candidate(PendingCandidate value) {
    webrtc::SdpParseError error;
    auto candidate = webrtc::IceCandidate::Create(value.mid, value.line,
                                                  value.candidate, &error);
    if (!candidate || !peer_->AddIceCandidate(candidate.get()))
      fail("remote-ice-candidate-rejected");
  }

  void on_track(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
    if (!transceiver || !transceiver->receiver() || !transceiver->receiver()->track())
      return;
    auto track = transceiver->receiver()->track();
    if (track->kind() != webrtc::MediaStreamTrackInterface::kAudioKind ||
        track->id() != "movie-audio")
      return;
    remote_track_ = webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
        static_cast<webrtc::AudioTrackInterface *>(track.get()));
    if (result_.connected)
      attach_sink();
  }

  void attach_sink() {
    if (remote_track_ && !sink_attached_) {
      remote_track_->AddSink(&sink_);
      sink_attached_ = true;
    }
  }

  void populate_result() {
    const auto snapshot = sink_.snapshot();
    result_.frames_received = snapshot.valid_callback_count;
    result_.invalid_frames_received = snapshot.invalid_callback_count;
    result_.sample_rate = snapshot.sample_rate;
    result_.channels = snapshot.channels;
    result_.peak = snapshot.peak;
    if (source_)
      result_.chunks_generated = source_->generated_count();
  }

  void collect_stats() {
    if (!runtime_ || !peer_)
      return;
    auto callback = webrtc::scoped_refptr<StatsObserver>(new StatsObserver());
    runtime_->signaling_thread()->BlockingCall([&] { peer_->GetStats(callback.get()); });
    auto report = callback->wait();
    if (!report)
      return;
    std::lock_guard lock(mu_);
    for (const auto *transport : report->GetStatsOfType<webrtc::RTCTransportStats>()) {
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

  void stop_source() noexcept {
    if (source_ && !source_stopped_.exchange(true, std::memory_order_acq_rel))
      source_->stop();
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

  MovieAudioPeerConfig config_;
  MovieAudioPeerCallbacks callbacks_;
  std::shared_ptr<WebRtcRuntime> runtime_;
  webrtc::scoped_refptr<LocalAudioSource> source_;
  CountingAudioSink sink_;
  std::unique_ptr<PeerObserver> observer_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> local_track_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> remote_track_;
  CandidateStager<PendingCandidate, 64> candidates_;
  bool remote_set_{};
  bool sink_attached_{};
  std::mutex mu_;
  MovieAudioPeerResult result_;
  std::atomic_bool wait_cancelled_{false};
  std::atomic_bool stopped_{false};
  std::atomic_bool source_stopped_{false};
  std::shared_ptr<std::atomic_bool> callbacks_active_{
      std::make_shared<std::atomic_bool>(true)};
};

std::unique_ptr<MovieAudioPeer>
MovieAudioPeer::create(MovieAudioPeerConfig config, MovieAudioPeerCallbacks callbacks) {
  auto impl = std::make_unique<Impl>(std::move(config), std::move(callbacks));
  if (!impl->initialize())
    return nullptr;
  return std::unique_ptr<MovieAudioPeer>(new MovieAudioPeer(std::move(impl)));
}

MovieAudioPeer::MovieAudioPeer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MovieAudioPeer::~MovieAudioPeer() = default;
bool MovieAudioPeer::start() { return impl_->start(); }
bool MovieAudioPeer::receive_description(std::string type, std::string sdp) {
  return impl_->receive_description(std::move(type), std::move(sdp));
}
bool MovieAudioPeer::receive_candidate(std::string mid, int line, std::string candidate) {
  return impl_->receive_candidate(std::move(mid), line, std::move(candidate));
}
MovieAudioPeerResult MovieAudioPeer::wait(std::chrono::milliseconds timeout) {
  return impl_->wait(timeout);
}
void MovieAudioPeer::cancel_wait() noexcept { impl_->cancel_wait(); }
void MovieAudioPeer::stop() noexcept { impl_->stop(); }

} // namespace shareme::rtc
