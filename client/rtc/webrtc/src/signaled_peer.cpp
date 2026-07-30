#include "shareme/rtc/signaled_peer.hpp"

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/ref_counted_base.h"
#include "api/rtp_receiver_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "audio_device_factory.hpp"
#include "counting_video_sink.hpp"
#include "rtc_base/thread.h"
#include "shareme/rtc/candidate_stager.hpp"
#include "test_pattern_source.hpp"
#include "webrtc_runtime.hpp"

namespace shareme::rtc {
namespace {
struct PendingCandidate { std::string mid; int line{}; std::string candidate; };

class CreateObserver final : public webrtc::CreateSessionDescriptionObserver, private webrtc::RefCountedBase {
 public:
  using Success=std::function<void(std::unique_ptr<webrtc::SessionDescriptionInterface>)>;
  CreateObserver(Success success,std::function<void(std::string)> failure):success_(std::move(success)),failure_(std::move(failure)){}
  void OnSuccess(webrtc::SessionDescriptionInterface* value) override { success_(std::unique_ptr<webrtc::SessionDescriptionInterface>(value)); }
  void OnFailure(webrtc::RTCError error) override { failure_(std::string(error.message())); }
  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override { return webrtc::RefCountedBase::Release(); }
 private: ~CreateObserver() override=default; Success success_; std::function<void(std::string)> failure_;
};
class SetObserver final : public webrtc::SetSessionDescriptionObserver, private webrtc::RefCountedBase {
 public:
  SetObserver(std::function<void()> success,std::function<void(std::string)> failure):success_(std::move(success)),failure_(std::move(failure)){}
  void OnSuccess() override { success_(); }
  void OnFailure(webrtc::RTCError error) override { failure_(std::string(error.message())); }
  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override { return webrtc::RefCountedBase::Release(); }
 private: ~SetObserver() override=default; std::function<void()> success_; std::function<void(std::string)> failure_;
};
class StatsObserver final : public webrtc::RTCStatsCollectorCallback, private webrtc::RefCountedBase {
 public:
  void OnStatsDelivered(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override { std::lock_guard lock(mu_); report_=report; ready_=true; cv_.notify_all(); }
  auto wait(){std::unique_lock lock(mu_);cv_.wait_for(lock,std::chrono::seconds(2),[&]{return ready_;});return report_;}
  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }
  webrtc::RefCountReleaseStatus Release() const override { return webrtc::RefCountedBase::Release(); }
 private: ~StatsObserver() override=default; std::mutex mu_;std::condition_variable cv_;bool ready_{};webrtc::scoped_refptr<const webrtc::RTCStatsReport> report_;
};
}

bool valid_remote_description(SignaledRole role,std::string_view type,std::string_view sdp) noexcept {
  return !sdp.empty() && ((role==SignaledRole::viewer&&type=="offer")||(role==SignaledRole::host&&type=="answer"));
}
bool valid_remote_candidate(std::string_view mid,int line,std::string_view candidate) noexcept { return !mid.empty()&&line>=0&&!candidate.empty(); }

class SignaledPeer::Impl final {
 public:
  Impl(SignaledRole role,SignaledPeerCallbacks callbacks):role_(role),callbacks_(std::move(callbacks)){}
  bool initialize(){
    auto audio=create_audio_device(webrtc::CreateEnvironment(),AudioDeviceMode::synthetic);
    if(!audio.ok()){fail(audio.message);return false;}
    runtime_=WebRtcRuntime::create(audio.device); if(!runtime_){fail("WebRTC runtime creation failed");return false;}
    queues_=webrtc::CreateDefaultTaskQueueFactory();
    video_source_=TestPatternSource::create(*queues_,640,360,30);
    return runtime_->signaling_thread()->BlockingCall([this]{return setup();});
  }
  bool start(){
    if(!runtime_||!peer_)return false;
    if(role_==SignaledRole::host) runtime_->signaling_thread()->PostTask([this]{create_offer();});
    return true;
  }
  bool receive_description(std::string type,std::string sdp){
    if(!valid_remote_description(role_,type,sdp)||!runtime_)return false;
    runtime_->signaling_thread()->PostTask([this,type=std::move(type),sdp=std::move(sdp)]() mutable {apply_remote(std::move(type),std::move(sdp));});
    return true;
  }
  bool receive_candidate(std::string mid,int line,std::string candidate){
    if(!valid_remote_candidate(mid,line,candidate)||!runtime_)return false;
    runtime_->signaling_thread()->PostTask([this,p=PendingCandidate{std::move(mid),line,std::move(candidate)}]() mutable {
      if(remote_set_) add_candidate(std::move(p)); else if(!candidates_.stage(std::move(p))) fail("pending ICE candidate limit exceeded");
    }); return true;
  }
  SignaledPeerResult wait(std::chrono::milliseconds timeout){
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<deadline){
      {std::lock_guard lock(mu_);if(!result_.error.empty())break;if(result_.connected&&sink_.frame_count()>0)break;}
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    collect_stats();
    std::lock_guard lock(mu_);result_.video_frames_received=sink_.frame_count();
    if(!result_.connected&&result_.error.empty())result_.error="signaled call timed out";
    if(result_.video_frames_received==0&&result_.error.empty())result_.error="no remote video received";
    if((result_.audio_packets_sent==0||result_.audio_packets_received==0)&&result_.error.empty())result_.error="no bidirectional audio RTP";
    return result_;
  }
  void stop(){
    if(stopped_.exchange(true))return;
    if(video_source_)video_source_->stop();
    if(remote_video_)remote_video_->RemoveSink(&sink_);
    if(runtime_&&runtime_->signaling_thread()&&runtime_->signaling_thread()->RunningForTest())runtime_->signaling_thread()->BlockingCall([this]{peer_=nullptr;observer_.reset();});
    if(runtime_)static_cast<void>(runtime_->stop());
  }
  ~Impl(){stop();}
 private:
  class PeerObserver final:public webrtc::PeerConnectionObserver{
   public: explicit PeerObserver(Impl& owner):owner_(owner){}
    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override{}
    void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override{}
    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override{}
    void OnIceCandidate(const webrtc::IceCandidate* value) override{if(value&&owner_.callbacks_.candidate)owner_.callbacks_.candidate(value->sdp_mid(),value->sdp_mline_index(),value->ToString());}
    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state) override{if(state==webrtc::PeerConnectionInterface::kIceConnectionFailed)owner_.fail("ICE connection failed");}
    void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState state) override{if(state==webrtc::PeerConnectionInterface::PeerConnectionState::kConnected){std::lock_guard lock(owner_.mu_);owner_.result_.connected=true;}else if(state==webrtc::PeerConnectionInterface::PeerConnectionState::kFailed)owner_.fail("PeerConnection failed");}
    void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> value) override{owner_.on_track(std::move(value));}
   private: Impl& owner_;
  };
  bool setup(){
    observer_=std::make_unique<PeerObserver>(*this);
    webrtc::PeerConnectionInterface::RTCConfiguration config;config.sdp_semantics=webrtc::SdpSemantics::kUnifiedPlan;
    auto created=runtime_->factory()->CreatePeerConnectionOrError(config,webrtc::PeerConnectionDependencies(observer_.get()));
    if(!created.ok()){fail("PeerConnection creation failed");return false;}peer_=std::move(created.value());peer_->SetAudioPlayout(false);
    video_track_=runtime_->factory()->CreateVideoTrack(video_source_,"movie-video");
    audio_source_=runtime_->factory()->CreateAudioSource(audio_options(AudioSourceKind::synthetic));
    audio_track_=runtime_->factory()->CreateAudioTrack(role_==SignaledRole::host?"host-voice":"viewer-voice",audio_source_.get());
    if(!video_track_||!audio_track_||!peer_->AddTrack(video_track_,{"shareme-test"}).ok()||!peer_->AddTrack(audio_track_,{"shareme-test"}).ok()){fail("adding test tracks failed");return false;}
    video_source_->start();return true;
  }
  void create_offer(){create_description(true);}
  void create_answer(){create_description(false);}
  void create_description(bool offer){
    auto observer=webrtc::scoped_refptr<CreateObserver>(new CreateObserver([this,offer](auto desc){on_local_description(offer,std::move(desc));},[this](auto error){fail("description creation failed: "+error);}));
    if(offer)peer_->CreateOffer(observer.get(),{});else peer_->CreateAnswer(observer.get(),{});
  }
  void on_local_description(bool offer,std::unique_ptr<webrtc::SessionDescriptionInterface> desc){
    std::string sdp;if(!desc->ToString(&sdp)){fail("SDP serialization failed");return;}
    auto observer=webrtc::scoped_refptr<SetObserver>(new SetObserver([this,offer,sdp=std::move(sdp)]() mutable {if(callbacks_.description)callbacks_.description(offer?"offer":"answer",std::move(sdp));},[this](auto error){fail("local description failed: "+error);}));
    peer_->SetLocalDescription(observer.get(),desc.release());
  }
  void apply_remote(std::string type,std::string sdp){
    const auto sdp_type=type=="offer"?webrtc::SdpType::kOffer:webrtc::SdpType::kAnswer;
    auto desc=webrtc::CreateSessionDescription(sdp_type,sdp);if(!desc){fail("remote SDP parsing failed");return;}
    auto observer=webrtc::scoped_refptr<SetObserver>(new SetObserver([this]{remote_set_=true;for(auto& candidate:candidates_.drain())add_candidate(std::move(candidate));if(role_==SignaledRole::viewer)create_answer();},[this](auto error){fail("remote description failed: "+error);}));
    peer_->SetRemoteDescription(observer.get(),desc.release());
  }
  void add_candidate(PendingCandidate value){webrtc::SdpParseError error;auto candidate=webrtc::IceCandidate::Create(value.mid,value.line,value.candidate,&error);if(!candidate||!peer_->AddIceCandidate(candidate.get()))fail("remote ICE candidate rejected");}
  void on_track(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> value){if(!value||!value->receiver()||!value->receiver()->track())return;auto track=value->receiver()->track();if(track->kind()==webrtc::MediaStreamTrackInterface::kVideoKind){remote_video_=static_cast<webrtc::VideoTrackInterface*>(track.get());remote_video_->AddOrUpdateSink(&sink_,{});}else if(track->kind()==webrtc::MediaStreamTrackInterface::kAudioKind){remote_audio_=static_cast<webrtc::AudioTrackInterface*>(track.get());remote_audio_->set_enabled(false);}}
  void collect_stats(){if(!runtime_||!peer_)return;auto callback=webrtc::scoped_refptr<StatsObserver>(new StatsObserver());runtime_->signaling_thread()->BlockingCall([&]{peer_->GetStats(callback.get());});auto report=callback->wait();if(!report)return;std::lock_guard lock(mu_);for(const auto* stats:report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>())if(stats->kind==std::optional<std::string>{"audio"})result_.audio_packets_sent+=stats->packets_sent.value_or(0);for(const auto* stats:report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>())if(stats->kind==std::optional<std::string>{"audio"})result_.audio_packets_received+=stats->packets_received.value_or(0);for(const auto* transport:report->GetStatsOfType<webrtc::RTCTransportStats>()){if(!transport->selected_candidate_pair_id)continue;const auto* pair=report->GetAs<webrtc::RTCIceCandidatePairStats>(*transport->selected_candidate_pair_id);if(!pair||!pair->local_candidate_id)continue;const auto* candidate=report->GetAs<webrtc::RTCLocalIceCandidateStats>(*pair->local_candidate_id);if(candidate&&candidate->candidate_type)result_.selected_candidate_type=*candidate->candidate_type;}}
  void fail(std::string error){std::lock_guard lock(mu_);if(result_.error.empty())result_.error=std::move(error);}
  SignaledRole role_;SignaledPeerCallbacks callbacks_;std::shared_ptr<WebRtcRuntime> runtime_;std::unique_ptr<webrtc::TaskQueueFactory> queues_;webrtc::scoped_refptr<TestPatternSource> video_source_;CountingVideoSink sink_;std::unique_ptr<PeerObserver> observer_;webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_;webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_,remote_video_;webrtc::scoped_refptr<webrtc::AudioSourceInterface> audio_source_;webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_,remote_audio_;CandidateStager<PendingCandidate,64> candidates_;bool remote_set_{};std::mutex mu_;SignaledPeerResult result_;std::atomic_bool stopped_{false};
};

std::unique_ptr<SignaledPeer> SignaledPeer::create(SignaledRole role,SignaledPeerCallbacks callbacks){auto impl=std::make_unique<Impl>(role,std::move(callbacks));if(!impl->initialize())return nullptr;return std::unique_ptr<SignaledPeer>(new SignaledPeer(std::move(impl)));}
SignaledPeer::SignaledPeer(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
SignaledPeer::~SignaledPeer()=default;
bool SignaledPeer::start(){return impl_->start();}
bool SignaledPeer::receive_description(std::string type,std::string sdp){return impl_->receive_description(std::move(type),std::move(sdp));}
bool SignaledPeer::receive_candidate(std::string mid,int line,std::string candidate){return impl_->receive_candidate(std::move(mid),line,std::move(candidate));}
SignaledPeerResult SignaledPeer::wait(std::chrono::milliseconds timeout){return impl_->wait(timeout);}
void SignaledPeer::stop() noexcept{impl_->stop();}
}
