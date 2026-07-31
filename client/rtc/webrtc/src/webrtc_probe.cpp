#include "shareme/rtc/webrtc_probe.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/media_stream_interface.h"
#include "api/ref_counted_base.h"
#include "api/rtp_receiver_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "audio_device_factory.hpp"
#include "counting_video_sink.hpp"
#include "loopback_signaling.hpp"
#include "microphone_permission.hpp"
#include "test_pattern_source.hpp"
#include "webrtc_runtime.hpp"

namespace shareme::rtc {
namespace {

constexpr std::size_t kMaximumDiagnosticBytes = 256;

class StatsCallback final : public webrtc::RTCStatsCollectorCallback,
                            private webrtc::RefCountedBase {
public:
  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport> &report)
      override {
    std::lock_guard lock(mutex_);
    report_ = report;
    delivered_ = true;
    delivered_condition_.notify_all();
  }

  [[nodiscard]] webrtc::scoped_refptr<const webrtc::RTCStatsReport>
  wait(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    delivered_condition_.wait_for(lock, timeout, [this] { return delivered_; });
    return report_;
  }

  void AddRef() const override { webrtc::RefCountedBase::AddRef(); }

  webrtc::RefCountReleaseStatus Release() const override {
    return webrtc::RefCountedBase::Release();
  }

private:
  std::mutex mutex_;
  std::condition_variable delivered_condition_;
  bool delivered_{false};
  webrtc::scoped_refptr<const webrtc::RTCStatsReport> report_;
};

std::string sanitize(std::string value) {
  if (value.size() > kMaximumDiagnosticBytes) {
    value.resize(kMaximumDiagnosticBytes);
  }
  std::replace_if(
      value.begin(), value.end(),
      [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return byte < 0x20U || byte == 0x7FU;
      },
      ' ');
  return value;
}

ProbeStatus status_for_audio_error(AudioDeviceError error) {
  switch (error) {
  case AudioDeviceError::permission_denied:
    return ProbeStatus::permission_denied;
  case AudioDeviceError::dependency_unavailable:
    return ProbeStatus::dependency_error;
  case AudioDeviceError::initialization_failed:
  case AudioDeviceError::none:
    return ProbeStatus::media_failed;
  }
  return ProbeStatus::media_failed;
}

struct StatsCollectionResult {
  webrtc::scoped_refptr<const webrtc::RTCStatsReport> report;
  bool timed_out{false};
};

StatsCollectionResult
collect_stats(WebRtcRuntime &runtime,
              webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer,
              std::chrono::milliseconds timeout) {
  if (peer == nullptr || runtime.signaling_thread() == nullptr) {
    return {};
  }
  auto callback = webrtc::scoped_refptr<StatsCallback>(new StatsCallback());
  runtime.signaling_thread()->BlockingCall(
      [&] { peer->GetStats(callback.get()); });
  auto report = callback->wait(timeout);
  return {
      .report = report,
      .timed_out = report == nullptr,
  };
}

void read_outbound_stats(const webrtc::RTCStatsReport &report,
                         ProbeResult &result) {
  for (const auto *stats :
       report.GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
    if (stats->kind != std::optional<std::string>{"audio"}) {
      continue;
    }
    result.audio_packets_sent += stats->packets_sent.value_or(0);
    result.audio_bytes_sent += stats->bytes_sent.value_or(0);
  }
  for (const auto *source :
       report.GetStatsOfType<webrtc::RTCAudioSourceStats>()) {
    if (source->audio_level.has_value()) {
      result.audio_level = source->audio_level;
      break;
    }
  }
  for (const auto *transport :
       report.GetStatsOfType<webrtc::RTCTransportStats>()) {
    if (!transport->selected_candidate_pair_id.has_value()) {
      continue;
    }
    const auto *pair = report.GetAs<webrtc::RTCIceCandidatePairStats>(
        *transport->selected_candidate_pair_id);
    if (pair == nullptr) {
      continue;
    }
    if (pair->current_round_trip_time.has_value()) {
      result.round_trip_time_ms = *pair->current_round_trip_time * 1'000.0;
    }
    if (!pair->local_candidate_id.has_value()) {
      continue;
    }
    const auto *candidate = report.GetAs<webrtc::RTCLocalIceCandidateStats>(
        *pair->local_candidate_id);
    if (candidate != nullptr && candidate->candidate_type.has_value()) {
      result.selected_candidate_type = *candidate->candidate_type;
    }
  }
}

void read_inbound_stats(const webrtc::RTCStatsReport &report,
                        ProbeResult &result) {
  for (const auto *stats :
       report.GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
    if (stats->kind != std::optional<std::string>{"audio"}) {
      continue;
    }
    result.audio_packets_received += stats->packets_received.value_or(0);
    result.audio_bytes_received += stats->bytes_received.value_or(0);
  }
}

} // namespace

ProbeResult run_webrtc_probe(const ProbeConfig &config) {
  using namespace std::chrono_literals;
  ProbeResult result;
  if (const auto error = validate(config); error.has_value()) {
    result.status = ProbeStatus::media_failed;
    result.diagnostic = sanitize(*error);
    return result;
  }

  const auto environment = webrtc::CreateEnvironment();
  const auto audio_mode = config.audio_mode == ProbeAudioMode::microphone
                              ? AudioDeviceMode::microphone
                              : AudioDeviceMode::synthetic;
  auto audio = create_audio_device(environment, audio_mode, {}, {}, [] {
    return platform_microphone_permission_status();
  });
  if (!audio.ok()) {
    result.status = status_for_audio_error(audio.error);
    result.diagnostic = sanitize(std::move(audio.message));
    return result;
  }

  auto runtime = WebRtcRuntime::create(audio.device);
  if (runtime == nullptr) {
    result.status = ProbeStatus::dependency_error;
    result.diagnostic = "WebRTC runtime creation failed";
    return result;
  }

  auto task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  auto video_source =
      TestPatternSource::create(*task_queue_factory, config.width,
                                config.height, config.frames_per_second);
  CountingVideoSink video_sink;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> remote_video_track;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> remote_audio_track;
  webrtc::scoped_refptr<webrtc::AudioSourceInterface> audio_source;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track;

  LoopbackMediaHooks hooks;
  hooks.configure =
      [&](webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory,
          webrtc::scoped_refptr<webrtc::PeerConnectionInterface> left,
          webrtc::scoped_refptr<webrtc::PeerConnectionInterface> right) {
        right->SetAudioPlayout(false);
        video_track = factory->CreateVideoTrack(video_source, "movie-video");
        const auto audio_kind = audio_mode == AudioDeviceMode::microphone
                                    ? AudioSourceKind::microphone
                                    : AudioSourceKind::synthetic;
        audio_source = factory->CreateAudioSource(audio_options(audio_kind));
        audio_track =
            factory->CreateAudioTrack("host-voice", audio_source.get());
        if (video_track == nullptr || audio_source == nullptr ||
            audio_track == nullptr) {
          return std::string{"probe media track creation failed"};
        }
        auto video_sender =
            left->AddTrack(video_track, {"shareme-probe-stream"});
        if (!video_sender.ok()) {
          return std::string{"adding probe video track failed"};
        }
        auto audio_sender =
            left->AddTrack(audio_track, {"shareme-probe-stream"});
        if (!audio_sender.ok()) {
          return std::string{"adding probe audio track failed"};
        }
        static_cast<void>(video_source->start());
        return std::string{};
      };
  hooks.remote_track =
      [&](LoopbackPeer peer,
          webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
        if (peer != LoopbackPeer::right || transceiver == nullptr ||
            transceiver->receiver() == nullptr ||
            transceiver->receiver()->track() == nullptr) {
          return;
        }
        if (transceiver->receiver()->track()->kind() ==
            webrtc::MediaStreamTrackInterface::kAudioKind) {
          remote_audio_track = static_cast<webrtc::AudioTrackInterface *>(
              transceiver->receiver()->track().get());
          remote_audio_track->set_enabled(false);
          return;
        }
        if (transceiver->receiver()->track()->kind() !=
            webrtc::MediaStreamTrackInterface::kVideoKind) {
          return;
        }
        remote_video_track = static_cast<webrtc::VideoTrackInterface *>(
            transceiver->receiver()->track().get());
        remote_video_track->AddOrUpdateSink(&video_sink, {});
      };

  LoopbackSignaling signaling(*runtime, std::move(hooks));
  LoopbackPeerConnections connections;
  const auto orderly_shutdown = [&] {
    video_source->stop();
    if (runtime->signaling_thread() != nullptr && runtime->threads_running()) {
      runtime->signaling_thread()->BlockingCall([&] {
        if (remote_video_track != nullptr) {
          remote_video_track->RemoveSink(&video_sink);
        }
      });
    }
    signaling.stop();
    if (runtime->signaling_thread() != nullptr && runtime->threads_running()) {
      runtime->signaling_thread()->BlockingCall([&] {
        connections.left = nullptr;
        connections.right = nullptr;
        remote_video_track = nullptr;
        remote_audio_track = nullptr;
        video_track = nullptr;
        audio_track = nullptr;
        audio_source = nullptr;
      });
    }
    return runtime->stop();
  };
  const auto connect_started = std::chrono::steady_clock::now();
  const auto negotiation = signaling.negotiate(config.connect_timeout);
  result.connection_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - connect_started);
  if (!negotiation.ok) {
    result.status = negotiation.error.find("timed out") != std::string::npos
                        ? ProbeStatus::timed_out
                        : ProbeStatus::negotiation_failed;
    result.diagnostic = sanitize(negotiation.error);
    if (!orderly_shutdown()) {
      result.status = ProbeStatus::shutdown_failed;
      result.diagnostic = "WebRTC runtime shutdown failed";
    }
    return result;
  }

  std::this_thread::sleep_for(config.run_for);
  video_source->stop();
  result.video_frames_sent = video_source->generated_count();
  result.video_frames_dropped = video_source->dropped_count();
  result.video_frames_received = video_sink.frame_count();

  connections = signaling.connections();
  const auto outbound = collect_stats(*runtime, connections.left, 500ms);
  const auto inbound = collect_stats(*runtime, connections.right, 500ms);
  if (outbound.report != nullptr) {
    read_outbound_stats(*outbound.report, result);
  }
  if (inbound.report != nullptr) {
    read_inbound_stats(*inbound.report, result);
  }

  const auto final_health = signaling.status();
  const bool shutdown_ok = orderly_shutdown();
  if (!shutdown_ok) {
    result.status = ProbeStatus::shutdown_failed;
    result.diagnostic = "WebRTC runtime shutdown failed";
    return result;
  }

  if (outbound.timed_out || inbound.timed_out) {
    result.status = ProbeStatus::timed_out;
    result.diagnostic = "WebRTC stats collection timed out";
    return result;
  }
  if (!final_health.ok) {
    result.status = ProbeStatus::negotiation_failed;
    result.diagnostic = final_health.error.empty()
                            ? "WebRTC transport became unhealthy"
                            : sanitize(final_health.error);
    return result;
  }
  if (result.video_frames_received == 0 || result.audio_packets_sent == 0 ||
      result.audio_packets_received == 0 || result.audio_bytes_sent == 0 ||
      result.audio_bytes_received == 0 ||
      result.selected_candidate_type.empty()) {
    result.status = ProbeStatus::media_failed;
    result.diagnostic = "probe media evidence was incomplete";
    return result;
  }
  result.status = ProbeStatus::passed;
  return result;
}

} // namespace shareme::rtc
