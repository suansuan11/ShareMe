#pragma once

#include "qt_signaling_client.hpp"
#include "playback_state.hpp"
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
#include <memory>
#include <filesystem>
#include <thread>

namespace shareme::rtc {
class MovieTimeline;
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

public:
  RtcDemoController(QUrl server_url, shareme::rtc::SignaledRole role,
                    QString requested_room, bool desktop_source,
                    std::filesystem::path movie_path, bool movie_audio,
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

private:
  bool createPeer();
  void startPeer();
  void stopPeer() noexcept;
  void setStatus(QString status);
  void setRoomId(QString room_id);
  void deliverRemoteFrame(const webrtc::VideoFrame &frame);
  void publishPlaybackState();
  void refreshHostPlayback();
  void receiveControlMessage(std::string message);

  QUrl server_url_;
  shareme::rtc::SignaledRole role_;
  QString requested_room_;
  bool desktop_source_{false};
  std::filesystem::path movie_path_;
  bool movie_audio_{false};
  std::shared_ptr<shareme::rtc::MovieTimeline> movie_timeline_;
  webrtc::scoped_refptr<shareme::rtc::LocalVideoSource> movie_video_source_;
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
  std::uint64_t playback_sequence_{1};
  shareme::tools::PlaybackStateTracker playback_tracker_;
  QString remote_playback_state_{QStringLiteral("unavailable")};
  qint64 remote_playback_position_ms_{0};
  QString host_playback_state_{QStringLiteral("unavailable")};
  qint64 host_playback_position_ms_{0};
  qint64 host_playback_start_ms_{0};
  qint64 host_playback_duration_ms_{0};
  qulonglong host_playback_generation_{0};
  bool host_controls_available_{false};
  bool peer_started_{false};
  bool start_requested_{false};
};
