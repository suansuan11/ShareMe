#pragma once

#include "qt_signaling_client.hpp"
#include "drift_metrics_jsonl.hpp"
#include "drift_failure.hpp"
#include "movie_audio_clock_message.hpp"
#include "playback_state.hpp"
#include "playout_report.hpp"
#include "shareme/core/drift_metrics.hpp"
#include "shareme/core/audio_route.hpp"
#include "shareme/core/movie_audio_renderer.hpp"
#include "movie_video_playout_adapter.hpp"
#include "qt_audio_route_monitor.hpp"
#include "drift_scenario.hpp"
#include "shareme/rtc/movie_audio_peer.hpp"
#include "shareme/rtc/signaled_peer.hpp"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVideoSink>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace shareme::rtc {
class MovieTimeline;
class MovieVideoSource;
}

class RtcDemoController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString roomId READ roomId NOTIFY roomIdChanged)
  Q_PROPERTY(bool viewer READ viewer CONSTANT)
  Q_PROPERTY(QString remotePlaybackState READ remotePlaybackState NOTIFY remotePlaybackChanged)
  Q_PROPERTY(qint64 remotePlaybackPositionMs READ remotePlaybackPositionMs NOTIFY remotePlaybackChanged)
  Q_PROPERTY(QString hostPlaybackState READ hostPlaybackState NOTIFY hostPlaybackChanged)
  Q_PROPERTY(qint64 hostPlaybackPositionMs READ hostPlaybackPositionMs NOTIFY hostPlaybackChanged)
  Q_PROPERTY(qint64 hostPlaybackStartMs READ hostPlaybackStartMs NOTIFY hostPlaybackChanged)
  Q_PROPERTY(qint64 hostPlaybackDurationMs READ hostPlaybackDurationMs NOTIFY hostPlaybackChanged)
  Q_PROPERTY(qulonglong hostPlaybackGeneration READ hostPlaybackGeneration NOTIFY hostPlaybackChanged)
  Q_PROPERTY(bool hostControlsAvailable READ hostControlsAvailable NOTIFY hostPlaybackChanged)
  Q_PROPERTY(qint64 viewerRenderedPositionMs READ viewerRenderedPositionMs NOTIFY playoutReportChanged)
  Q_PROPERTY(qint64 hostViewerDeltaMs READ hostViewerDeltaMs NOTIFY playoutReportChanged)
  Q_PROPERTY(QString hostSyncAction READ hostSyncAction NOTIFY playoutReportChanged)
  Q_PROPERTY(bool viewerRenderedAvailable READ viewerRenderedAvailable NOTIFY playoutReportChanged)
  Q_PROPERTY(QString viewerSuggestedAction READ viewerSuggestedAction NOTIFY playoutReportChanged)
  Q_PROPERTY(QString viewerAppliedAction READ viewerAppliedAction NOTIFY playoutReportChanged)
  Q_PROPERTY(QString audioClockConfidence READ audioClockConfidence NOTIFY playoutReportChanged)
  Q_PROPERTY(qulonglong audioRouteGeneration READ audioRouteGeneration NOTIFY playoutReportChanged)
  Q_PROPERTY(qulonglong audioRendererQueueDurationMs READ audioRendererQueueDurationMs NOTIFY playoutReportChanged)
  Q_PROPERTY(qulonglong audioDeviceQueueDurationMs READ audioDeviceQueueDurationMs NOTIFY playoutReportChanged)
  Q_PROPERTY(qulonglong audioUnderrunCount READ audioUnderrunCount NOTIFY playoutReportChanged)
  Q_PROPERTY(QString audioLastDiscontinuityCategory READ audioLastDiscontinuityCategory NOTIFY playoutReportChanged)
  Q_PROPERTY(QString driftScenarioPhase READ driftScenarioPhase NOTIFY driftScenarioChanged)
  Q_PROPERTY(bool driftScenarioActive READ driftScenarioActive NOTIFY driftScenarioChanged)

public:
  RtcDemoController(QUrl server_url, shareme::rtc::SignaledRole role,
                    QString requested_room, bool desktop_source,
                    std::filesystem::path movie_path, bool movie_audio,
                    QString video_acceleration,
                    QString metrics_jsonl_path, QString drift_scenario_name,
                    qint64 measurement_duration_seconds,
                    QObject *parent = nullptr);
  ~RtcDemoController() override;

  RtcDemoController(const RtcDemoController &) = delete;
  RtcDemoController &operator=(const RtcDemoController &) = delete;

  [[nodiscard]] QString status() const;
  [[nodiscard]] QString roomId() const;
  [[nodiscard]] bool viewer() const noexcept;
  [[nodiscard]] QString remotePlaybackState() const;
  [[nodiscard]] qint64 remotePlaybackPositionMs() const noexcept;
  [[nodiscard]] QString hostPlaybackState() const;
  [[nodiscard]] qint64 hostPlaybackPositionMs() const noexcept;
  [[nodiscard]] qint64 hostPlaybackStartMs() const noexcept;
  [[nodiscard]] qint64 hostPlaybackDurationMs() const noexcept;
  [[nodiscard]] qulonglong hostPlaybackGeneration() const noexcept;
  [[nodiscard]] bool hostControlsAvailable() const noexcept;
  [[nodiscard]] qint64 viewerRenderedPositionMs() const noexcept;
  [[nodiscard]] qint64 hostViewerDeltaMs() const noexcept;
  [[nodiscard]] QString hostSyncAction() const;
  [[nodiscard]] bool viewerRenderedAvailable() const noexcept;
  [[nodiscard]] QString viewerSuggestedAction() const;
  [[nodiscard]] QString viewerAppliedAction() const;
  [[nodiscard]] QString audioClockConfidence() const;
  [[nodiscard]] qulonglong audioRouteGeneration() const noexcept;
  [[nodiscard]] qulonglong audioRendererQueueDurationMs() const noexcept;
  [[nodiscard]] qulonglong audioDeviceQueueDurationMs() const noexcept;
  [[nodiscard]] qulonglong audioUnderrunCount() const noexcept;
  [[nodiscard]] QString audioLastDiscontinuityCategory() const;
  [[nodiscard]] QString driftScenarioPhase() const;
  [[nodiscard]] bool driftScenarioActive() const noexcept;

  Q_INVOKABLE void setVideoSink(QVideoSink *sink);
  Q_INVOKABLE void start();
  Q_INVOKABLE void pauseHostPlayback();
  Q_INVOKABLE void resumeHostPlayback();
  Q_INVOKABLE void seekHostPlayback(qint64 absolute_pts_ms);

signals:
  void statusChanged();
  void roomIdChanged();
  void remotePlaybackChanged();
  void hostPlaybackChanged();
  void playoutReportChanged();
  void driftScenarioChanged();

private:
  bool createPeer();
  void startPeer();
  void stopPeer() noexcept;
  void setStatus(QString status);
  void setRoomId(QString room_id);
  void deliverRemoteFrame(const webrtc::VideoFrame &frame);
  void recordRenderedFrame(std::uint32_t rtp_timestamp);
  void pumpMovieAudio();
  void publishPlaybackState();
  void publishPlayoutReport();
  void refreshHostPlayback();
  void receiveControlMessage(std::string message);
  void flushDriftMetrics();
  void stopDriftMetrics() noexcept;
  void runDriftScenario();
  void failDriftScenario(const QString& category);
  void recordDriftError(std::string category, bool notify_viewer = true);
  void emitDriftDiagnostics();
  void emitPerformanceCounters();
  void startAudioRouteMonitor();
  void handleAudioRouteEvent(shareme::core::AudioRouteEvent event);
  void refreshAudioDiagnostics(
      const shareme::core::MovieAudioRendererSnapshot &audio,
      const shareme::core::VideoSchedulerSnapshot &scheduler);

  QUrl server_url_;
  shareme::rtc::SignaledRole role_;
  QString requested_room_;
  bool desktop_source_{false};
  std::filesystem::path movie_path_;
  bool movie_audio_{false};
  QString video_acceleration_{QStringLiteral("software")};
  QString metrics_jsonl_path_;
  QString drift_scenario_name_;
  qint64 measurement_duration_seconds_{0};
  std::shared_ptr<shareme::rtc::MovieTimeline> movie_timeline_;
  webrtc::scoped_refptr<shareme::rtc::MovieVideoSource> movie_video_source_;
  QString status_{QStringLiteral("idle")};
  QString room_id_;
  QPointer<QVideoSink> video_sink_;
  QtSignalingClient signaling_;
  std::unique_ptr<shareme::rtc::SignaledPeer> peer_;
  std::unique_ptr<shareme::rtc::MovieAudioPeer> movie_peer_;
  std::unique_ptr<shareme::core::MovieAudioRenderer> movie_audio_renderer_;
  std::unique_ptr<QtAudioRouteMonitor> audio_route_monitor_;
  shareme::core::AudioRouteController audio_route_controller_;
  std::jthread waiter_;
  std::jthread movie_waiter_;
  std::unique_ptr<shareme::tools::MovieVideoPlayoutAdapter>
      movie_video_playout_adapter_;
  QTimer playback_state_timer_;
  QTimer playout_report_timer_;
  QTimer movie_audio_pump_timer_;
  QTimer drift_metrics_flush_timer_;
  QTimer drift_scenario_timer_;
  QTimer performance_timer_;
  std::uint64_t playback_sequence_{1};
  std::uint64_t scheduler_observation_sequence_{1};
  std::chrono::steady_clock::time_point scheduler_started_at_{};
  shareme::tools::PlaybackStateTracker playback_tracker_;
  mutable std::mutex playback_anchor_mutex_;
  shareme::tools::PlayoutReportTracker playout_report_tracker_;
  std::optional<shareme::tools::PlaybackState> viewer_playback_anchor_;
  shareme::tools::MovieAudioClockTracker movie_audio_clock_tracker_;
  shareme::tools::RenderedPlayoutTracker rendered_playout_tracker_;
  std::chrono::steady_clock::time_point viewer_anchor_received_at_{};
  std::int64_t rendered_sample_time_ms_{0};
  std::uint64_t playout_report_sequence_{1};
  std::uint64_t drift_sample_index_{0};
  std::deque<shareme::core::DriftSample> pending_drift_samples_;
  shareme::core::DriftAggregator drift_aggregator_;
  std::unique_ptr<shareme::tools::DriftMetricsJsonlWriter>
      drift_metrics_writer_;
  bool drift_capture_enabled_{false};
  std::optional<shareme::tools::DriftScenario> drift_scenario_;
  std::chrono::steady_clock::time_point drift_scenario_started_at_{};
  shareme::core::DriftPhase drift_phase_{shareme::core::DriftPhase::warmup};
  bool drift_scenario_started_{false};
  bool drift_scenario_failed_{false};
  std::size_t drift_report_messages_{0};
  std::size_t drift_report_receive_attempts_{0};
  std::size_t drift_report_decode_successes_{0};
  std::size_t drift_sink_submissions_{0};
  std::size_t drift_report_encode_attempts_{0};
  std::size_t drift_report_encode_successes_{0};
  std::size_t drift_report_send_attempts_{0};
  std::shared_ptr<std::atomic<std::size_t>> drift_report_send_successes_{
      std::make_shared<std::atomic<std::size_t>>(0)};
  std::chrono::steady_clock::time_point last_drift_diagnostic_at_{};
  std::string selected_candidate_type_;
  bool drift_diagnostics_enabled_{false};
  bool performance_counters_enabled_{false};
  std::atomic<std::uint64_t> performance_callback_count_{0};
  std::atomic<std::uint64_t> performance_coalesced_count_{0};
  std::atomic<std::uint64_t> performance_sink_submissions_{0};
  std::atomic<std::uint64_t> performance_conversion_failures_{0};
  std::atomic<std::uint64_t> performance_fallback_copies_{0};
  std::atomic<int> performance_frame_width_{0};
  std::atomic<int> performance_frame_height_{0};
  std::jthread performance_stats_worker_;
  std::mutex performance_stats_mutex_;
  std::mutex performance_stats_wait_mutex_;
  std::condition_variable_any performance_stats_wait_;
  shareme::rtc::SignaledVideoStats performance_video_stats_{
      .unavailable = true};
  QString remote_playback_state_{QStringLiteral("unavailable")};
  qint64 remote_playback_position_ms_{0};
  QString host_playback_state_{QStringLiteral("unavailable")};
  qint64 host_playback_position_ms_{0};
  qint64 host_playback_start_ms_{0};
  qint64 host_playback_duration_ms_{0};
  qulonglong host_playback_generation_{0};
  bool host_controls_available_{false};
  qint64 viewer_rendered_position_ms_{0};
  qint64 host_viewer_delta_ms_{0};
  QString host_sync_action_{QStringLiteral("unavailable")};
  bool viewer_rendered_available_{false};
  QString viewer_suggested_action_{QStringLiteral("none")};
  QString viewer_applied_action_{QStringLiteral("none")};
  QString audio_clock_confidence_{QStringLiteral("unavailable")};
  qulonglong audio_route_generation_{0};
  qulonglong audio_renderer_queue_duration_ms_{0};
  qulonglong audio_device_queue_duration_ms_{0};
  qulonglong audio_underrun_count_{0};
  QString audio_last_discontinuity_category_{QStringLiteral("none")};
  bool audio_route_transition_pending_{false};
  bool peer_started_{false};
  bool movie_audio_output_ready_{true};
  bool start_requested_{false};
  bool shutting_down_{false};
};
