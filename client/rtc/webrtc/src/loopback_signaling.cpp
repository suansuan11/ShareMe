#include "loopback_signaling.hpp"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "api/data_channel_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "api/ref_count.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sctp_transport_interface.h"
#include "rtc_base/thread.h"
#include "shareme/rtc/candidate_stager.hpp"
#include "webrtc_runtime.hpp"

namespace shareme::rtc {
namespace {

struct PendingCandidate {
  std::string sdp_mid;
  int sdp_mline_index{0};
  std::string candidate;
};

class CreateDescriptionObserver final
    : public webrtc::CreateSessionDescriptionObserver,
      private webrtc::RefCountedBase {
public:
  using Success =
      std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface>)>;
  using Failure = std::function<void(webrtc::RTCError)>;

  CreateDescriptionObserver(Success success, Failure failure)
      : success_(std::move(success)), failure_(std::move(failure)) {}

  void OnSuccess(webrtc::SessionDescriptionInterface *description) override {
    success_(std::unique_ptr<webrtc::SessionDescriptionInterface>(description));
  }

  void OnFailure(webrtc::RTCError error) override {
    failure_(std::move(error));
  }

  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }

  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  ~CreateDescriptionObserver() override = default;

  Success success_;
  Failure failure_;
};

class SetDescriptionObserver final
    : public webrtc::SetSessionDescriptionObserver,
      private webrtc::RefCountedBase {
public:
  using Success = std::function<void()>;
  using Failure = std::function<void(webrtc::RTCError)>;

  SetDescriptionObserver(Success success, Failure failure)
      : success_(std::move(success)), failure_(std::move(failure)) {}

  void OnSuccess() override { success_(); }

  void OnFailure(webrtc::RTCError error) override {
    failure_(std::move(error));
  }

  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }

  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  ~SetDescriptionObserver() override = default;

  Success success_;
  Failure failure_;
};

} // namespace

class LoopbackSignaling::Impl final {
public:
  explicit Impl(WebRtcRuntime &runtime) : runtime_(runtime) {
    shutdown_hook_ = runtime_.register_shutdown_hook(
        this, [this] { on_runtime_shutdown(); });
    if (shutdown_hook_ == nullptr) {
      stopped_ = true;
      set_failure("WebRTC runtime is not accepting sessions");
    }
  }

  ~Impl() {
    stop();
    if (shutdown_hook_ != nullptr &&
        !runtime_shutdown_.load(std::memory_order_acquire)) {
      runtime_.unregister_shutdown_hook(this, shutdown_hook_);
    }
  }

  LoopbackNegotiationResult negotiate(std::chrono::milliseconds timeout) {
    {
      std::lock_guard lock(state_mutex_);
      if (negotiation_started_) {
        auto repeated_result = result_;
        repeated_result.ok = false;
        repeated_result.error = "loopback negotiation may only start once";
        return repeated_result;
      }
      negotiation_started_ = true;
    }
    if (!runtime_.threads_running() || runtime_.signaling_thread() == nullptr) {
      set_failure("WebRTC runtime is not running");
      return snapshot();
    }

    runtime_.signaling_thread()->BlockingCall([this] { begin_on_signaling(); });
    std::unique_lock lock(state_mutex_);
    state_changed_.wait_for(
        lock, timeout, [this] { return result_.ok || !result_.error.empty(); });
    if (!result_.ok && result_.error.empty()) {
      result_.error = "loopback negotiation timed out";
    }
    auto result = result_;
    lock.unlock();
    if (!result.ok) {
      stop();
    }
    return result;
  }

  bool stage_candidate_for_test(LoopbackPeer destination,
                                std::string candidate) {
    if (!runtime_.threads_running() || runtime_.signaling_thread() == nullptr) {
      set_failure("WebRTC runtime is not running");
      return false;
    }
    return runtime_.signaling_thread()->BlockingCall(
        [this, destination, candidate = std::move(candidate)]() mutable {
          PendingCandidate pending{
              .sdp_mid = "0",
              .sdp_mline_index = 0,
              .candidate = std::move(candidate),
          };
          auto &stager = destination == LoopbackPeer::left ? left_candidates_
                                                           : right_candidates_;
          if (stager.stage(std::move(pending))) {
            return true;
          }
          set_failure("pending ICE candidate limit 64 exceeded");
          return false;
        });
  }

  std::string failure() const {
    std::lock_guard lock(state_mutex_);
    return result_.error;
  }

  void stop() noexcept {
    std::lock_guard stop_lock(stop_mutex_);
    if (stopped_) {
      return;
    }
    stopped_ = true;
    callbacks_active_->store(false, std::memory_order_release);
    if (runtime_.signaling_thread() != nullptr &&
        runtime_.signaling_thread()->RunningForTest()) {
      if (runtime_.signaling_thread()->IsCurrent()) {
        cleanup_on_signaling();
      } else {
        auto completed = std::make_shared<std::promise<void>>();
        auto completion = completed->get_future();
        runtime_.signaling_thread()->PostTask([this, completed] {
          cleanup_on_signaling();
          completed->set_value();
        });
        completion.wait();
      }
    }
  }

private:
  void on_runtime_shutdown() {
    stop();
    runtime_shutdown_.store(true, std::memory_order_release);
  }

  class PeerObserver final : public webrtc::PeerConnectionObserver {
  public:
    PeerObserver(Impl &owner, LoopbackPeer peer,
                 std::shared_ptr<std::atomic_bool> callbacks_active)
        : owner_(owner), peer_(peer),
          callbacks_active_(std::move(callbacks_active)) {}

    void OnSignalingChange(
        webrtc::PeerConnectionInterface::SignalingState) override {}

    void OnDataChannel(
        webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {
      if (callbacks_active()) {
        owner_.on_data_channel(peer_, std::move(channel));
      }
    }

    void OnIceGatheringChange(
        webrtc::PeerConnectionInterface::IceGatheringState) override {}

    void OnIceCandidate(const webrtc::IceCandidate *candidate) override {
      if (callbacks_active()) {
        owner_.on_ice_candidate(peer_, *candidate);
      }
    }

    void OnIceConnectionChange(
        webrtc::PeerConnectionInterface::IceConnectionState state) override {
      if (callbacks_active()) {
        owner_.on_ice_state(peer_, state);
      }
    }

    void OnConnectionChange(
        webrtc::PeerConnectionInterface::PeerConnectionState state) override {
      if (callbacks_active()) {
        owner_.on_connection_state(peer_, state);
      }
    }

  private:
    bool callbacks_active() const {
      return callbacks_active_->load(std::memory_order_acquire);
    }

    Impl &owner_;
    LoopbackPeer peer_;
    std::shared_ptr<std::atomic_bool> callbacks_active_;
  };

  void cleanup_on_signaling() {
    left_connection_ = nullptr;
    right_connection_ = nullptr;
    left_data_channel_ = nullptr;
    right_data_channel_ = nullptr;
    left_observer_.reset();
    right_observer_.reset();
    left_candidates_.clear();
    right_candidates_.clear();
  }

  void begin_on_signaling() {
    if (stopped_) {
      set_failure("loopback signaling is stopped");
      return;
    }
    auto factory = runtime_.factory();
    if (factory == nullptr) {
      set_failure("WebRTC peer connection factory is unavailable");
      return;
    }

    left_observer_ = std::make_unique<PeerObserver>(*this, LoopbackPeer::left,
                                                    callbacks_active_);
    right_observer_ = std::make_unique<PeerObserver>(*this, LoopbackPeer::right,
                                                     callbacks_active_);

    webrtc::PeerConnectionInterface::RTCConfiguration configuration;
    configuration.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

    auto left = factory->CreatePeerConnectionOrError(
        configuration,
        webrtc::PeerConnectionDependencies(left_observer_.get()));
    if (!left.ok()) {
      set_failure("left PeerConnection creation failed: " +
                  std::string(left.error().message()));
      return;
    }
    left_connection_ = std::move(left.value());

    auto right = factory->CreatePeerConnectionOrError(
        configuration,
        webrtc::PeerConnectionDependencies(right_observer_.get()));
    if (!right.ok()) {
      set_failure("right PeerConnection creation failed: " +
                  std::string(right.error().message()));
      return;
    }
    right_connection_ = std::move(right.value());

    auto channel =
        left_connection_->CreateDataChannelOrError("shareme-probe", nullptr);
    if (!channel.ok()) {
      set_failure("data channel creation failed: " +
                  std::string(channel.error().message()));
      return;
    }
    left_data_channel_ = std::move(channel.value());

    auto observer = webrtc::scoped_refptr<
        CreateDescriptionObserver>(new CreateDescriptionObserver(
        [this, callbacks_active = callbacks_active_](
            std::unique_ptr<webrtc::SessionDescriptionInterface> description) {
          if (callbacks_active->load(std::memory_order_acquire)) {
            on_offer_created(std::move(description));
          }
        },
        [this, callbacks_active = callbacks_active_](webrtc::RTCError error) {
          if (callbacks_active->load(std::memory_order_acquire)) {
            set_failure("offer creation failed: " +
                        std::string(error.message()));
          }
        }));
    left_connection_->CreateOffer(
        observer.get(),
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
  }

  void on_offer_created(
      std::unique_ptr<webrtc::SessionDescriptionInterface> description) {
    std::string sdp;
    if (!description->ToString(&sdp)) {
      set_failure("offer serialization failed");
      return;
    }
    pending_offer_sdp_ = std::move(sdp);
    auto observer = make_set_observer(
        [this] {
          left_local_offer_set_ = true;
          maybe_apply_offer_to_right();
        },
        "setting left local offer failed");
    left_connection_->SetLocalDescription(observer.get(),
                                          description.release());
  }

  void apply_offer_to_right() {
    auto offer = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer,
                                                  pending_offer_sdp_);
    if (offer == nullptr) {
      set_failure("offer parsing failed");
      return;
    }
    auto observer = make_set_observer(
        [this] {
          right_remote_description_set_ = true;
          drain_candidates(LoopbackPeer::right);
          create_answer();
        },
        "setting right remote offer failed");
    right_connection_->SetRemoteDescription(observer.get(), offer.release());
  }

  void maybe_apply_offer_to_right() {
    if (!left_local_offer_set_ || right_offer_application_started_ ||
        right_candidates_.empty()) {
      return;
    }
    right_offer_application_started_ = true;
    apply_offer_to_right();
  }

  void create_answer() {
    auto observer = webrtc::scoped_refptr<
        CreateDescriptionObserver>(new CreateDescriptionObserver(
        [this, callbacks_active = callbacks_active_](
            std::unique_ptr<webrtc::SessionDescriptionInterface> description) {
          if (callbacks_active->load(std::memory_order_acquire)) {
            on_answer_created(std::move(description));
          }
        },
        [this, callbacks_active = callbacks_active_](webrtc::RTCError error) {
          if (callbacks_active->load(std::memory_order_acquire)) {
            set_failure("answer creation failed: " +
                        std::string(error.message()));
          }
        }));
    right_connection_->CreateAnswer(
        observer.get(),
        webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
  }

  void on_answer_created(
      std::unique_ptr<webrtc::SessionDescriptionInterface> description) {
    std::string sdp;
    if (!description->ToString(&sdp)) {
      set_failure("answer serialization failed");
      return;
    }
    pending_answer_sdp_ = std::move(sdp);
    auto observer = make_set_observer(
        [this] {
          right_local_answer_set_ = true;
          maybe_apply_answer_to_left();
        },
        "setting right local answer failed");
    right_connection_->SetLocalDescription(observer.get(),
                                           description.release());
  }

  void apply_answer_to_left() {
    auto answer = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer,
                                                   pending_answer_sdp_);
    if (answer == nullptr) {
      set_failure("answer parsing failed");
      return;
    }
    auto observer = make_set_observer(
        [this] {
          left_remote_description_set_ = true;
          drain_candidates(LoopbackPeer::left);
          update_connected_result();
        },
        "setting left remote answer failed");
    left_connection_->SetRemoteDescription(observer.get(), answer.release());
  }

  void maybe_apply_answer_to_left() {
    if (!right_local_answer_set_ || left_answer_application_started_ ||
        left_candidates_.empty()) {
      return;
    }
    left_answer_application_started_ = true;
    apply_answer_to_left();
  }

  webrtc::scoped_refptr<SetDescriptionObserver>
  make_set_observer(std::function<void()> success, std::string failure_prefix) {
    return webrtc::scoped_refptr<SetDescriptionObserver>(
        new SetDescriptionObserver(
            [success = std::move(success),
             callbacks_active = callbacks_active_]() mutable {
              if (callbacks_active->load(std::memory_order_acquire)) {
                success();
              }
            },
            [this, failure_prefix = std::move(failure_prefix),
             callbacks_active = callbacks_active_](webrtc::RTCError error) {
              if (callbacks_active->load(std::memory_order_acquire)) {
                set_failure(failure_prefix + ": " +
                            std::string(error.message()));
              }
            }));
  }

  void on_ice_candidate(LoopbackPeer source,
                        const webrtc::IceCandidate &candidate) {
    PendingCandidate pending{
        .sdp_mid = candidate.sdp_mid(),
        .sdp_mline_index = candidate.sdp_mline_index(),
        .candidate = candidate.ToString(),
    };
    const auto destination =
        source == LoopbackPeer::left ? LoopbackPeer::right : LoopbackPeer::left;
    if (remote_description_set(destination)) {
      add_candidate(destination, std::move(pending));
      return;
    }

    auto &stager = destination == LoopbackPeer::left ? left_candidates_
                                                     : right_candidates_;
    if (!stager.stage(std::move(pending))) {
      set_failure("pending ICE candidate limit 64 exceeded");
      return;
    }
    if (destination == LoopbackPeer::right) {
      maybe_apply_offer_to_right();
    } else {
      maybe_apply_answer_to_left();
    }
  }

  bool remote_description_set(LoopbackPeer peer) const {
    return peer == LoopbackPeer::left ? left_remote_description_set_
                                      : right_remote_description_set_;
  }

  void drain_candidates(LoopbackPeer destination) {
    auto &stager = destination == LoopbackPeer::left ? left_candidates_
                                                     : right_candidates_;
    auto candidates = stager.drain();
    {
      std::lock_guard lock(state_mutex_);
      result_.drained_candidate_count += candidates.size();
    }
    for (auto &candidate : candidates) {
      add_candidate(destination, std::move(candidate));
    }
  }

  void add_candidate(LoopbackPeer destination, PendingCandidate pending) {
    webrtc::SdpParseError parse_error;
    auto candidate =
        webrtc::IceCandidate::Create(pending.sdp_mid, pending.sdp_mline_index,
                                     pending.candidate, &parse_error);
    if (candidate == nullptr) {
      set_failure("remote ICE candidate parsing failed");
      return;
    }
    auto connection = destination == LoopbackPeer::left ? left_connection_
                                                        : right_connection_;
    if (!connection->AddIceCandidate(candidate.get())) {
      set_failure("remote ICE candidate application failed");
    }
  }

  void on_ice_state(LoopbackPeer peer,
                    webrtc::PeerConnectionInterface::IceConnectionState state) {
    const bool connected =
        state == webrtc::PeerConnectionInterface::kIceConnectionConnected ||
        state == webrtc::PeerConnectionInterface::kIceConnectionCompleted;
    {
      std::lock_guard lock(state_mutex_);
      if (peer == LoopbackPeer::left) {
        result_.left_ice_connected = connected;
      } else {
        result_.right_ice_connected = connected;
      }
    }
    if (state == webrtc::PeerConnectionInterface::kIceConnectionFailed) {
      set_failure("ICE connection failed");
      return;
    }
    update_connected_result();
  }

  void on_connection_state(
      LoopbackPeer peer,
      webrtc::PeerConnectionInterface::PeerConnectionState state) {
    if (state ==
        webrtc::PeerConnectionInterface::PeerConnectionState::kFailed) {
      set_failure("peer connection failed");
      return;
    }
    if (state ==
        webrtc::PeerConnectionInterface::PeerConnectionState::kConnected) {
      auto connection =
          peer == LoopbackPeer::left ? left_connection_ : right_connection_;
      auto sctp = connection->GetSctpTransport();
      auto dtls = sctp == nullptr ? nullptr : sctp->dtls_transport();
      const bool connected =
          dtls != nullptr &&
          dtls->Information().state() == webrtc::DtlsTransportState::kConnected;
      {
        std::lock_guard lock(state_mutex_);
        if (peer == LoopbackPeer::left) {
          result_.left_dtls_connected = connected;
        } else {
          result_.right_dtls_connected = connected;
        }
      }
    }
    update_connected_result();
  }

  void
  on_data_channel(LoopbackPeer peer,
                  webrtc::scoped_refptr<webrtc::DataChannelInterface> channel) {
    if (peer == LoopbackPeer::right) {
      right_data_channel_ = std::move(channel);
    }
  }

  void update_connected_result() {
    std::lock_guard lock(state_mutex_);
    if (result_.error.empty() && result_.left_ice_connected &&
        result_.right_ice_connected && result_.left_dtls_connected &&
        result_.right_dtls_connected) {
      result_.ok = true;
      state_changed_.notify_all();
    }
  }

  void set_failure(std::string error) {
    std::lock_guard lock(state_mutex_);
    if (result_.error.empty()) {
      result_.error = std::move(error);
      state_changed_.notify_all();
    }
  }

  LoopbackNegotiationResult snapshot() const {
    std::lock_guard lock(state_mutex_);
    return result_;
  }

  WebRtcRuntime &runtime_;
  std::mutex stop_mutex_;
  mutable std::mutex state_mutex_;
  std::condition_variable state_changed_;
  LoopbackNegotiationResult result_;
  std::shared_ptr<std::atomic_bool> callbacks_active_{
      std::make_shared<std::atomic_bool>(true)};
  WebRtcRuntime::ShutdownHookHandle shutdown_hook_;
  std::atomic_bool runtime_shutdown_{false};
  bool stopped_{false};
  bool negotiation_started_{false};
  bool left_remote_description_set_{false};
  bool right_remote_description_set_{false};
  bool left_local_offer_set_{false};
  bool right_offer_application_started_{false};
  bool right_local_answer_set_{false};
  bool left_answer_application_started_{false};
  std::string pending_offer_sdp_;
  std::string pending_answer_sdp_;
  CandidateStager<PendingCandidate, 64> left_candidates_;
  CandidateStager<PendingCandidate, 64> right_candidates_;
  std::unique_ptr<PeerObserver> left_observer_;
  std::unique_ptr<PeerObserver> right_observer_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> left_connection_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> right_connection_;
  webrtc::scoped_refptr<webrtc::DataChannelInterface> left_data_channel_;
  webrtc::scoped_refptr<webrtc::DataChannelInterface> right_data_channel_;
};

LoopbackSignaling::LoopbackSignaling(WebRtcRuntime &runtime)
    : impl_(std::make_unique<Impl>(runtime)) {}

LoopbackSignaling::~LoopbackSignaling() = default;

LoopbackNegotiationResult
LoopbackSignaling::negotiate(std::chrono::milliseconds timeout) {
  return impl_->negotiate(timeout);
}

bool LoopbackSignaling::stage_candidate_for_test(LoopbackPeer destination,
                                                 std::string candidate) {
  return impl_->stage_candidate_for_test(destination, std::move(candidate));
}

std::string LoopbackSignaling::failure() const { return impl_->failure(); }

void LoopbackSignaling::stop() noexcept { impl_->stop(); }

} // namespace shareme::rtc
