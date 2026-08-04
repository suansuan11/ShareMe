#pragma once

#include "qt_signaling_client.hpp"
#include "drift_metrics_jsonl.hpp"
#include "playback_state.hpp"
#include "playout_report.hpp"
#include "shareme/core/drift_metrics.hpp"
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
#include <deque>
#include <memory>
#include <filesystem>
#include <optional>
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

public:
  RtcDemoController(QUrl server_url, shareme::rtc::SignaledRole role,
                    QString requested_room, bool desktop_source,
                    std::filesystem::path movie_path, bool movie_audio,
                    QString metrics_jsonl_path,
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

private:
  bool createPeer();
  void startPeer();
  void stopPeer() noexcept;
  void setStatus(QString status);
  void setRoomId(QString room_id);
  void deliverRemoteFrame(const webrtc::VideoFrame &frame);
  void recordRenderedFrame(std::uint32_t rtp_timestamp);
  void publishPlaybackState();
  void publishPlayoutReport();
  void refreshHostPlayback();
  void receiveControlMessage(std::string message);
  void flushDriftMetrics();
  void stopDriftMetrics() noexcept;

  QUrl server_url_;
  shareme::rtc::SignaledRole role_;
  QString requested_room_;
  bool desktop_source_{false};
  std::filesystem::path movie_path_;
  bool movie_audio_{false};
  QString metrics_jsonl_path_;
  std::shared_ptr<shareme::rtc::MovieTimeline> movie_timeline_;
  webrtc::scoped_refptr<shareme::rtc::MovieVideoSource> movie_video_source_;
  QString status_{QStringLiteral("idle")};
  QString room_id_;
  QPointer<QVideoSink> video_sink_;
  QtSignalingClient signaling_;
  std::unique_ptr<shareme::rtc::SignaledPeer> peer_;
  std::unique_ptr<shareme::rtc::MovieAudioPeer> movie_peer_;
  std::jthread waiter_;
  std::jthread movie_waiter_;
  std::atomic_bool video_delivery_pending_{false};
  QTimer playback_state_timer_;
  QTimer playout_report_timer_;
  QTimer drift_metrics_flush_timer_;
  std::uint64_t playback_sequence_{1};
  shareme::tools::PlaybackStateTracker playback_tracker_;
  shareme::tools::PlayoutReportTracker playout_report_tracker_;
  std::optional<shareme::tools::PlaybackState> viewer_playback_anchor_;
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
  bool peer_started_{false};
  bool start_requested_{false};
};
