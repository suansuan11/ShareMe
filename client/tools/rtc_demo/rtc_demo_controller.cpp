#include "rtc_demo_controller.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QVideoFrame>
#include <QVideoSink>

#include <chrono>
#include <iostream>
#include <limits>
#include <utility>

#include "libyuv/convert_argb.h"
#include "shareme/core/sync_controller.hpp"

#if defined(SHAREME_HAS_DESKTOP_CAPTURE)
#include "shareme/rtc/desktop_capture_source.hpp"
#endif
#if defined(SHAREME_HAS_MOVIE_RTC)
#include "shareme/rtc/movie_audio_source.hpp"
#include "shareme/rtc/movie_timeline.hpp"
#include "shareme/rtc/movie_video_source.hpp"
#endif

namespace {

QByteArray description_payload(const std::string &type,
                               const std::string &sdp) {
  return QJsonDocument(
             QJsonObject{{"descriptionType", QString::fromStdString(type)},
                         {"sdp", QString::fromStdString(sdp)}})
      .toJson(QJsonDocument::Compact);
}

QByteArray candidate_payload(const std::string &mid, int line,
                             const std::string &candidate) {
  return QJsonDocument(
             QJsonObject{{"sdpMid", QString::fromStdString(mid)},
                         {"sdpMLineIndex", line},
                         {"candidate", QString::fromStdString(candidate)}})
      .toJson(QJsonDocument::Compact);
}

QString sync_action_name(shareme::core::SyncAction action) {
  switch (action) {
  case shareme::core::SyncAction::none:
    return QStringLiteral("none");
  case shareme::core::SyncAction::adjust_buffer:
    return QStringLiteral("adjust-buffer");
  case shareme::core::SyncAction::adjust_rate:
    return QStringLiteral("adjust-rate");
  case shareme::core::SyncAction::hard_resync:
    return QStringLiteral("hard-resync-observed");
  }
  return QStringLiteral("unavailable");
}

} // namespace

RtcDemoController::RtcDemoController(QUrl server_url,
                                     shareme::rtc::SignaledRole role,
                                     QString requested_room,
                                     bool desktop_source,
                                     std::filesystem::path movie_path,
                                     bool movie_audio, QObject *parent)
    : QObject(parent), server_url_(std::move(server_url)), role_(role),
      requested_room_(std::move(requested_room)),
      desktop_source_(desktop_source), movie_path_(std::move(movie_path)),
      movie_audio_(movie_audio) {
  playback_state_timer_.setInterval(100);
  connect(&playback_state_timer_, &QTimer::timeout, this,
          &RtcDemoController::publishPlaybackState);
  playout_report_timer_.setInterval(250);
  connect(&playout_report_timer_, &QTimer::timeout, this,
          &RtcDemoController::publishPlayoutReport);
  connect(&signaling_, &QtSignalingClient::statusChanged, this,
          [this](const QString &state) {
            setStatus(state);
            if (state != QStringLiteral("connected"))
              return;
            if (role_ == shareme::rtc::SignaledRole::host)
              signaling_.createRoom();
            else
              signaling_.joinRoom(requested_room_);
          });
  connect(&signaling_, &QtSignalingClient::roomReady, this,
          [this](const QString &room) {
            setRoomId(room);
            if (!createPeer())
              return;
            if (role_ == shareme::rtc::SignaledRole::viewer)
              startPeer();
            else
              setStatus(QStringLiteral("waiting-for-viewer"));
          });
  connect(&signaling_, &QtSignalingClient::relayReceived, this,
          [this](const QString &type, const QByteArray &raw) {
            if (type == QStringLiteral("participant-joined") &&
                role_ == shareme::rtc::SignaledRole::host) {
              startPeer();
              return;
            }
            const auto object = QJsonDocument::fromJson(raw).object();
            if (type == QStringLiteral("session-description") && peer_) {
              if (!peer_->receive_description(
                      object.value("descriptionType").toString().toStdString(),
                      object.value("sdp").toString().toStdString()))
                setStatus(QStringLiteral("remote-description-rejected"));
            } else if (type == QStringLiteral("ice-candidate") && peer_) {
              if (!peer_->receive_candidate(
                      object.value("sdpMid").toString().toStdString(),
                      object.value("sdpMLineIndex").toInt(),
                      object.value("candidate").toString().toStdString()))
                setStatus(QStringLiteral("remote-candidate-rejected"));
            } else if (type ==
                           QStringLiteral("movie-audio-session-description") &&
                       movie_peer_) {
              if (!movie_peer_->receive_description(
                      object.value("descriptionType").toString().toStdString(),
                      object.value("sdp").toString().toStdString()))
                setStatus(QStringLiteral("movie-audio-description-rejected"));
            } else if (type == QStringLiteral("movie-audio-ice-candidate") &&
                       movie_peer_) {
              if (!movie_peer_->receive_candidate(
                      object.value("sdpMid").toString().toStdString(),
                      object.value("sdpMLineIndex").toInt(),
                      object.value("candidate").toString().toStdString()))
                setStatus(QStringLiteral("movie-audio-candidate-rejected"));
            }
          });
  connect(&signaling_, &QtSignalingClient::failed, this,
          [this](const QString &code) {
            setStatus(QStringLiteral("signaling-error: ") + code);
          });
}

RtcDemoController::~RtcDemoController() {
  signaling_.disconnectFromServer();
  stopPeer();
}

QString RtcDemoController::status() const { return status_; }

QString RtcDemoController::roomId() const { return room_id_; }

bool RtcDemoController::viewer() const noexcept {
  return role_ == shareme::rtc::SignaledRole::viewer;
}

QString RtcDemoController::remotePlaybackState() const {
  return remote_playback_state_;
}

qint64 RtcDemoController::remotePlaybackPositionMs() const noexcept {
  return remote_playback_position_ms_;
}

QString RtcDemoController::hostPlaybackState() const {
  return host_playback_state_;
}

qint64 RtcDemoController::hostPlaybackPositionMs() const noexcept {
  return host_playback_position_ms_;
}

qint64 RtcDemoController::hostPlaybackStartMs() const noexcept {
  return host_playback_start_ms_;
}

qint64 RtcDemoController::hostPlaybackDurationMs() const noexcept {
  return host_playback_duration_ms_;
}

qulonglong RtcDemoController::hostPlaybackGeneration() const noexcept {
  return host_playback_generation_;
}

bool RtcDemoController::hostControlsAvailable() const noexcept {
  return host_controls_available_;
}

qint64 RtcDemoController::viewerRenderedPositionMs() const noexcept {
  return viewer_rendered_position_ms_;
}

qint64 RtcDemoController::hostViewerDeltaMs() const noexcept {
  return host_viewer_delta_ms_;
}

QString RtcDemoController::hostSyncAction() const { return host_sync_action_; }

bool RtcDemoController::viewerRenderedAvailable() const noexcept {
  return viewer_rendered_available_;
}

void RtcDemoController::setVideoSink(QVideoSink *sink) { video_sink_ = sink; }

void RtcDemoController::start() {
  if (start_requested_)
    return;
  start_requested_ = true;
  setStatus(QStringLiteral("connecting"));
  signaling_.connectTo(server_url_);
}

void RtcDemoController::pauseHostPlayback() {
#if defined(SHAREME_HAS_MOVIE_RTC)
  if (!host_controls_available_ || !movie_timeline_ ||
      !movie_timeline_->pause())
    return;
  refreshHostPlayback();
  publishPlaybackState();
#endif
}

void RtcDemoController::resumeHostPlayback() {
#if defined(SHAREME_HAS_MOVIE_RTC)
  if (!host_controls_available_ || !movie_timeline_ ||
      !movie_timeline_->resume())
    return;
  refreshHostPlayback();
  publishPlaybackState();
#endif
}

void RtcDemoController::seekHostPlayback(qint64 absolute_pts_ms) {
#if defined(SHAREME_HAS_MOVIE_RTC)
  if (!host_controls_available_ || !movie_timeline_ ||
      !movie_timeline_->seek(static_cast<std::int64_t>(absolute_pts_ms)))
    return;
  refreshHostPlayback();
  publishPlaybackState();
#else
  static_cast<void>(absolute_pts_ms);
#endif
}

bool RtcDemoController::createPeer() {
  if (peer_)
    return true;
  shareme::rtc::SignaledPeerCallbacks callbacks;
  callbacks.description = [this](std::string type, std::string sdp) {
    QMetaObject::invokeMethod(
        &signaling_,
        [this, type = std::move(type), sdp = std::move(sdp)] {
          signaling_.relay(QStringLiteral("session-description"),
                           description_payload(type, sdp));
        },
        Qt::QueuedConnection);
  };
  callbacks.candidate =
      [this](std::string mid, int line, std::string candidate) {
        QMetaObject::invokeMethod(
            &signaling_,
            [this, mid = std::move(mid), line,
             candidate = std::move(candidate)] {
              signaling_.relay(QStringLiteral("ice-candidate"),
                               candidate_payload(mid, line, candidate));
            },
            Qt::QueuedConnection);
      };
  callbacks.failure = [this](std::string category) {
    QMetaObject::invokeMethod(
        this,
        [this, category = std::move(category)] {
          setStatus(QStringLiteral("peer-error: ") +
                    QString::fromStdString(category));
        },
        Qt::QueuedConnection);
  };
  shareme::rtc::SignaledPeerConfig config{.role = role_};
#if defined(SHAREME_HAS_DESKTOP_CAPTURE)
  if (desktop_source_) {
    config.video_mode = shareme::rtc::SignaledVideoMode::injected;
    config.video_source_factory = [](webrtc::TaskQueueFactory &)
        -> webrtc::scoped_refptr<shareme::rtc::LocalVideoSource> {
      return shareme::rtc::DesktopCaptureSource::create();
    };
  }
#endif
#if defined(SHAREME_HAS_MOVIE_RTC)
  if (!movie_path_.empty()) {
    movie_timeline_ = std::make_shared<shareme::rtc::MovieTimeline>();
    movie_video_source_ =
        shareme::rtc::MovieVideoSource::create(movie_path_, movie_timeline_);
    config.video_mode = shareme::rtc::SignaledVideoMode::injected;
    config.video_source_factory = [source = movie_video_source_](
                                      webrtc::TaskQueueFactory &)
        -> webrtc::scoped_refptr<shareme::rtc::LocalVideoSource> {
      return source;
    };
  }
#endif
  if (role_ == shareme::rtc::SignaledRole::host) {
    config.local_video_frame =
        [this](const webrtc::VideoFrame &frame) { deliverRemoteFrame(frame); };
  }
  if (role_ == shareme::rtc::SignaledRole::viewer) {
    config.remote_video_frame =
        [this](const webrtc::VideoFrame &frame) { deliverRemoteFrame(frame); };
  }
  config.control_message = [this](std::string message) {
    QMetaObject::invokeMethod(
        this, [this, message = std::move(message)] {
          receiveControlMessage(std::move(message));
        }, Qt::QueuedConnection);
  };
  peer_ = shareme::rtc::SignaledPeer::create(std::move(config),
                                             std::move(callbacks));
  if (!peer_) {
    setStatus(QStringLiteral("peer-creation-failed"));
    return false;
  }
#if defined(SHAREME_HAS_MOVIE_RTC)
  if (movie_audio_ || role_ == shareme::rtc::SignaledRole::viewer) {
    shareme::rtc::MovieAudioPeerCallbacks movie_callbacks;
    movie_callbacks.description = [this](std::string type, std::string sdp) {
      QMetaObject::invokeMethod(
          &signaling_, [this, type = std::move(type), sdp = std::move(sdp)] {
            signaling_.relay(QStringLiteral("movie-audio-session-description"),
                             description_payload(type, sdp));
          }, Qt::QueuedConnection);
    };
    movie_callbacks.candidate =
        [this](std::string mid, int line, std::string candidate) {
          QMetaObject::invokeMethod(
              &signaling_, [this, mid = std::move(mid), line,
               candidate = std::move(candidate)] {
                signaling_.relay(QStringLiteral("movie-audio-ice-candidate"),
                                 candidate_payload(mid, line, candidate));
              }, Qt::QueuedConnection);
        };
    movie_callbacks.failure = [this](std::string category) {
      QMetaObject::invokeMethod(
          this, [this, category = std::move(category)] {
            setStatus(QStringLiteral("movie-audio-error: ") +
                      QString::fromStdString(category));
          }, Qt::QueuedConnection);
    };
    shareme::rtc::MovieAudioPeerConfig movie_config{.role = role_};
    movie_config.native_playout =
        role_ == shareme::rtc::SignaledRole::viewer;
    if (movie_audio_) {
      movie_config.source_factory = [movie_path = movie_path_,
                                     timeline = movie_timeline_] {
        return shareme::rtc::MovieAudioSource::create(movie_path, timeline);
      };
    }
    movie_peer_ = shareme::rtc::MovieAudioPeer::create(
        std::move(movie_config), std::move(movie_callbacks));
    if (!movie_peer_) {
      setStatus(QStringLiteral("movie-audio-peer-creation-failed"));
      return false;
    }
  }
#endif
  return true;
}

void RtcDemoController::startPeer() {
  if (peer_started_ || !peer_)
    return;
  peer_started_ = peer_->start();
  if (!peer_started_) {
    setStatus(QStringLiteral("peer-start-failed"));
    return;
  }
  if (movie_peer_ && movie_peer_->start()) {
    movie_waiter_ = std::jthread([this] {
      const auto result = movie_peer_->wait(std::chrono::seconds(15));
      if (!result.error.empty()) {
        QMetaObject::invokeMethod(
            this, [this, error = result.error] {
              setStatus(QStringLiteral("movie-audio-error: ") +
                        QString::fromStdString(error));
            }, Qt::QueuedConnection);
      }
    });
  }
  setStatus(QStringLiteral("negotiating"));
  if (role_ == shareme::rtc::SignaledRole::host && !movie_path_.empty())
    refreshHostPlayback();
  if (role_ == shareme::rtc::SignaledRole::host && !movie_path_.empty())
    playback_state_timer_.start();
  if (role_ == shareme::rtc::SignaledRole::viewer)
    playout_report_timer_.start();
  waiter_ = std::jthread([this] {
    const auto result = peer_->wait(std::chrono::seconds(15));
    QMetaObject::invokeMethod(
        this,
        [this, result] {
          if (result.error.empty()) {
            setStatus(QStringLiteral("connected"));
          } else {
            setStatus(QStringLiteral("call-error: ") +
                      QString::fromStdString(result.error));
          }
        },
        Qt::QueuedConnection);
  });
}

void RtcDemoController::stopPeer() noexcept {
  playback_state_timer_.stop();
  playout_report_timer_.stop();
  if (peer_)
    peer_->cancel_wait();
  if (movie_peer_)
    movie_peer_->cancel_wait();
  if (waiter_.joinable())
    waiter_.join();
  if (movie_waiter_.joinable())
    movie_waiter_.join();
  if (movie_peer_)
    movie_peer_->stop();
  if (peer_)
    peer_->stop();
  peer_.reset();
  movie_peer_.reset();
}

void RtcDemoController::publishPlaybackState() {
#if defined(SHAREME_HAS_MOVIE_RTC)
  if (!peer_ || !movie_video_source_ || !movie_timeline_ || room_id_.isEmpty())
    return;
  const auto timeline = movie_timeline_->snapshot();
  if (!timeline)
    return;
  const auto video_sample = movie_video_source_->last_frame_sample();
  if (!video_sample || video_sample->generation != timeline->generation)
    return;
  refreshHostPlayback();
  const auto now = std::chrono::steady_clock::now();
  const auto effective = std::chrono::duration_cast<std::chrono::milliseconds>(
                             now.time_since_epoch())
                             .count();
  const auto ended = movie_video_source_->state() ==
                     webrtc::MediaSourceInterface::kEnded;
  const auto state = shareme::tools::make_movie_playback_state(
      room_id_, playback_sequence_, timeline->media_pts_ms, effective,
      ended || timeline->state == shareme::rtc::MovieTimelineState::paused
          ? shareme::tools::MoviePlaybackState::paused
          : shareme::tools::MoviePlaybackState::playing,
      timeline->generation, video_sample->media_pts_ms,
      video_sample->rtp_timestamp);
  if (!state)
    return;
  if (!peer_->send_control_message(
          shareme::tools::encode_playback_state(*state).toStdString()))
    return;
  ++playback_sequence_;
  if (ended)
    playback_state_timer_.stop();
  else
    playback_state_timer_.setInterval(1'000);
#endif
}

void RtcDemoController::publishPlayoutReport() {
  const auto &rendered_sample = rendered_playout_tracker_.last();
  if (!viewer() || !peer_ || room_id_.isEmpty() || !rendered_sample ||
      !viewer_playback_anchor_ ||
      !viewer_playback_anchor_->video_anchor_media_pts_ms ||
      !viewer_playback_anchor_->video_rtp_timestamp ||
      viewer_playback_anchor_->state != QStringLiteral("playing") ||
      rendered_sample->generation != viewer_playback_anchor_->generation)
    return;
  const shareme::tools::PlayoutReport report{
      .room_id = room_id_,
      .sequence = playout_report_sequence_,
      .rendered_pts_ms = rendered_sample->rendered_pts_ms,
      .buffer_ms = rendered_sample->buffer_ms,
      .receive_time_ms = rendered_sample_time_ms_,
      .generation = rendered_sample->generation};
  const auto encoded = shareme::tools::encode_playout_report(report);
  if (encoded.isEmpty() ||
      !peer_->send_control_message(encoded.toStdString()))
    return;
  ++playout_report_sequence_;
}

void RtcDemoController::refreshHostPlayback() {
#if defined(SHAREME_HAS_MOVIE_RTC)
  QString state = QStringLiteral("unavailable");
  qint64 position = 0;
  qint64 start = 0;
  qint64 duration = 0;
  qulonglong generation = 0;
  bool available = false;
  if (role_ == shareme::rtc::SignaledRole::host && movie_timeline_ &&
      movie_video_source_) {
    if (const auto timeline = movie_timeline_->snapshot()) {
      const auto ended = movie_video_source_->state() ==
                         webrtc::MediaSourceInterface::kEnded;
      state = ended || timeline->state == shareme::rtc::MovieTimelineState::paused
                  ? QStringLiteral("paused")
                  : QStringLiteral("playing");
      position = static_cast<qint64>(timeline->media_pts_ms);
      start = static_cast<qint64>(timeline->start_pts_ms);
      duration = static_cast<qint64>(timeline->duration_ms);
      generation = static_cast<qulonglong>(timeline->generation);
      available = peer_started_ && !ended;
    }
  }
  if (host_playback_state_ == state && host_playback_position_ms_ == position &&
      host_playback_start_ms_ == start &&
      host_playback_duration_ms_ == duration &&
      host_playback_generation_ == generation &&
      host_controls_available_ == available)
    return;
  host_playback_state_ = std::move(state);
  host_playback_position_ms_ = position;
  host_playback_start_ms_ = start;
  host_playback_duration_ms_ = duration;
  host_playback_generation_ = generation;
  host_controls_available_ = available;
  emit hostPlaybackChanged();
#endif
}

void RtcDemoController::receiveControlMessage(std::string message) {
  if (room_id_.isEmpty())
    return;
  const auto bytes = QByteArray::fromStdString(message);
  if (viewer()) {
    const auto state =
        shareme::tools::decode_playback_state(bytes, room_id_);
    if (!state || !playback_tracker_.accept(*state))
      return;
    if (!state->video_anchor_media_pts_ms || !state->video_rtp_timestamp ||
        !viewer_playback_anchor_ ||
        viewer_playback_anchor_->generation != state->generation) {
      rendered_playout_tracker_.reset();
      viewer_rendered_position_ms_ = 0;
      viewer_rendered_available_ = false;
      emit playoutReportChanged();
    }
    viewer_playback_anchor_ = *state;
    viewer_anchor_received_at_ = std::chrono::steady_clock::now();
    remote_playback_state_ = state->state;
    remote_playback_position_ms_ = static_cast<qint64>(state->media_pts_ms);
    emit remotePlaybackChanged();
    return;
  }
#if defined(SHAREME_HAS_MOVIE_RTC)
  if (!movie_timeline_)
    return;
  const auto timeline = movie_timeline_->snapshot();
  const auto report = shareme::tools::decode_playout_report(bytes, room_id_);
  if (!timeline || !report ||
      !playout_report_tracker_.accept(*report, timeline->generation))
    return;
  const auto delta = shareme::tools::viewer_delta_ms(
      timeline->media_pts_ms, report->rendered_pts_ms);
  if (!delta)
    return;
  const auto decision = shareme::core::SyncController{}.decide(*delta);
  viewer_rendered_position_ms_ = static_cast<qint64>(report->rendered_pts_ms);
  host_viewer_delta_ms_ = static_cast<qint64>(*delta);
  host_sync_action_ = sync_action_name(decision.action);
  viewer_rendered_available_ = true;
  emit playoutReportChanged();
#endif
}

void RtcDemoController::setStatus(QString status) {
  if (status_ == status)
    return;
  status_ = std::move(status);
  emit statusChanged();
}

void RtcDemoController::setRoomId(QString room_id) {
  if (room_id_ == room_id)
    return;
  room_id_ = std::move(room_id);
  std::cout << "ROOM " << room_id_.toStdString() << std::endl;
  emit roomIdChanged();
}

void RtcDemoController::deliverRemoteFrame(const webrtc::VideoFrame &frame) {
  if (video_delivery_pending_.exchange(true, std::memory_order_acq_rel))
    return;
  const auto release_delivery = [this] {
    video_delivery_pending_.store(false, std::memory_order_release);
  };
  const auto source_buffer = frame.video_frame_buffer();
  const auto rtp_timestamp = frame.rtp_timestamp();
  const auto buffer = source_buffer ? source_buffer->ToI420() : nullptr;
  if (!buffer) {
    release_delivery();
    return;
  }
  QImage image(buffer->width(), buffer->height(), QImage::Format_ARGB32);
  if (image.isNull()) {
    release_delivery();
    return;
  }
  const auto converted = libyuv::I420ToARGB(
      buffer->DataY(), buffer->StrideY(), buffer->DataU(), buffer->StrideU(),
      buffer->DataV(), buffer->StrideV(), image.bits(), image.bytesPerLine(),
      buffer->width(), buffer->height());
  if (converted != 0) {
    release_delivery();
    return;
  }
  const auto queued = QMetaObject::invokeMethod(
      this,
      [this, image = std::move(image), rtp_timestamp] {
        if (video_sink_)
          video_sink_->setVideoFrame(QVideoFrame(image));
        if (viewer())
          recordRenderedFrame(rtp_timestamp);
        video_delivery_pending_.store(false, std::memory_order_release);
      },
      Qt::QueuedConnection);
  if (!queued)
    release_delivery();
}

void RtcDemoController::recordRenderedFrame(std::uint32_t rtp_timestamp) {
  if (!viewer_playback_anchor_)
    return;
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - viewer_anchor_received_at_)
                           .count();
  const auto sample = shareme::tools::reconcile_rendered_frame(
      *viewer_playback_anchor_, rtp_timestamp, elapsed);
  if (!sample)
    return;
  if (!rendered_playout_tracker_.accept(*viewer_playback_anchor_, *sample))
    return;
  rendered_sample_time_ms_ =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count();
  if (viewer_rendered_position_ms_ !=
      static_cast<qint64>(sample->rendered_pts_ms)) {
    viewer_rendered_position_ms_ =
        static_cast<qint64>(sample->rendered_pts_ms);
    emit playoutReportChanged();
  }
  if (!viewer_rendered_available_) {
    viewer_rendered_available_ = true;
    emit playoutReportChanged();
  }
}
