#include "rtc_demo_controller.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QCoreApplication>
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

QString drift_phase_name(shareme::core::DriftPhase phase) {
  switch (phase) {
  case shareme::core::DriftPhase::warmup:
    return QStringLiteral("warmup");
  case shareme::core::DriftPhase::steady:
    return QStringLiteral("steady");
  case shareme::core::DriftPhase::paused:
    return QStringLiteral("paused");
  case shareme::core::DriftPhase::post_resume:
    return QStringLiteral("post-resume");
  case shareme::core::DriftPhase::post_forward_seek:
    return QStringLiteral("post-forward-seek");
  case shareme::core::DriftPhase::post_backward_seek:
    return QStringLiteral("post-backward-seek");
  case shareme::core::DriftPhase::cooldown:
    return QStringLiteral("cooldown");
  }
  return QStringLiteral("unknown");
}

} // namespace

RtcDemoController::RtcDemoController(QUrl server_url,
                                     shareme::rtc::SignaledRole role,
                                     QString requested_room,
                                     bool desktop_source,
                                     std::filesystem::path movie_path,
                                     bool movie_audio, QString metrics_jsonl_path,
                                     QString drift_scenario_name,
                                     qint64 measurement_duration_seconds,
                                     QObject *parent)
    : QObject(parent), server_url_(std::move(server_url)), role_(role),
      requested_room_(std::move(requested_room)),
      desktop_source_(desktop_source), movie_path_(std::move(movie_path)),
      movie_audio_(movie_audio), metrics_jsonl_path_(std::move(metrics_jsonl_path)),
      drift_scenario_name_(std::move(drift_scenario_name)),
      measurement_duration_seconds_(measurement_duration_seconds) {
  playback_state_timer_.setInterval(100);
  connect(&playback_state_timer_, &QTimer::timeout, this,
          &RtcDemoController::publishPlaybackState);
  playout_report_timer_.setInterval(250);
  connect(&playout_report_timer_, &QTimer::timeout, this,
          &RtcDemoController::publishPlayoutReport);
  connect(&signaling_, &QtSignalingClient::statusChanged, this,
          [this](const QString &state) {
            setStatus(state);
            if (drift_scenario_ && drift_scenario_started_ &&
                !drift_scenario_failed_ && !drift_scenario_->completed() &&
                state != QStringLiteral("connected")) {
              failDriftScenario(QStringLiteral("connection-lost"));
              return;
            }
            if (state != QStringLiteral("connected"))
              return;
            if (role_ == shareme::rtc::SignaledRole::host)
              signaling_.createRoom();
            else
              signaling_.joinRoom(requested_room_);
          });
  drift_metrics_flush_timer_.setInterval(1'000);
  connect(&drift_metrics_flush_timer_, &QTimer::timeout, this,
          &RtcDemoController::flushDriftMetrics);
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

QString RtcDemoController::driftScenarioPhase() const {
  return drift_phase_name(drift_phase_);
}

bool RtcDemoController::driftScenarioActive() const noexcept {
  return drift_scenario_.has_value() && drift_scenario_started_ &&
      !drift_scenario_failed_ && !drift_scenario_->completed();
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
  if (role_ == shareme::rtc::SignaledRole::host && !movie_path_.empty() &&
      !metrics_jsonl_path_.isEmpty()) {
    drift_metrics_writer_ = std::make_unique<shareme::tools::DriftMetricsJsonlWriter>(
        metrics_jsonl_path_);
    if (drift_metrics_writer_->open()) {
      drift_capture_enabled_ = true;
      drift_metrics_flush_timer_.start();
    } else {
      setStatus(QStringLiteral("drift-capture-disabled: ") +
                drift_metrics_writer_->failure_category());
      drift_metrics_writer_.reset();
    }
  }
  if (role_ == shareme::rtc::SignaledRole::host && !movie_path_.empty())
    refreshHostPlayback();
  if (role_ == shareme::rtc::SignaledRole::host && !movie_path_.empty())
    playback_state_timer_.start();
  if (role_ == shareme::rtc::SignaledRole::viewer)
    playout_report_timer_.start();
  if (role_ == shareme::rtc::SignaledRole::host &&
      drift_scenario_name_ == QStringLiteral("drift-study-v1")) {
    drift_scenario_.emplace();
    drift_scenario_timer_.setInterval(100);
    connect(&drift_scenario_timer_, &QTimer::timeout, this,
            &RtcDemoController::runDriftScenario);
    drift_scenario_timer_.start();
  }
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
  drift_scenario_timer_.stop();
  stopDriftMetrics();
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

void RtcDemoController::flushDriftMetrics() {
  if (!drift_capture_enabled_ || !drift_metrics_writer_)
    return;
  while (!pending_drift_samples_.empty()) {
    if (!drift_metrics_writer_->append(pending_drift_samples_.front())) {
      drift_capture_enabled_ = false;
      pending_drift_samples_.clear();
      setStatus(QStringLiteral("drift-capture-disabled: ") +
                drift_metrics_writer_->failure_category());
      return;
    }
    pending_drift_samples_.pop_front();
  }
}

void RtcDemoController::stopDriftMetrics() noexcept {
  drift_metrics_flush_timer_.stop();
  if (!drift_metrics_writer_)
    return;
  if (!drift_capture_enabled_) {
    drift_metrics_writer_.reset();
    pending_drift_samples_.clear();
    return;
  }
  flushDriftMetrics();
  if (!drift_capture_enabled_)
    return;
  if (!drift_scenario_failed_)
    drift_aggregator_.complete_run();
  const auto finalized = drift_metrics_writer_->finalize(drift_aggregator_.summary());
  if (!finalized) {
    drift_capture_enabled_ = false;
    setStatus(QStringLiteral("drift-capture-disabled: ") +
              drift_metrics_writer_->failure_category());
  }
  drift_metrics_writer_.reset();
}

void RtcDemoController::runDriftScenario() {
#if defined(SHAREME_HAS_MOVIE_RTC)
  if (!drift_scenario_ || drift_scenario_failed_ || !movie_timeline_)
    return;
  const auto timeline = movie_timeline_->snapshot();
  if (!timeline)
    return;
  if (!drift_scenario_started_) {
    const auto end_pts_ms = timeline->start_pts_ms + timeline->duration_ms;
    if (!shareme::tools::has_drift_study_duration(
            timeline->start_pts_ms, timeline->media_pts_ms, end_pts_ms)) {
      failDriftScenario(QStringLiteral("insufficient-duration"));
      return;
    }
    drift_scenario_started_at_ = std::chrono::steady_clock::now();
    drift_scenario_started_ = true;
    drift_phase_ = shareme::core::DriftPhase::warmup;
    return;
  }

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() -
                              drift_scenario_started_at_)
                              .count();
  const auto next_phase = drift_scenario_->phase_at(elapsed_ms);
  if (drift_phase_ != next_phase) {
    drift_phase_ = next_phase;
    emit driftScenarioChanged();
  }
  for (const auto& event : drift_scenario_->advance(elapsed_ms)) {
    if (event.action == shareme::tools::DriftScenarioAction::complete) {
      drift_scenario_timer_.stop();
      drift_aggregator_.complete_run();
      const auto summary = drift_aggregator_.summary();
      std::cout << "RESULT drift-study-v1 status=complete accepted_samples="
                << summary.accepted_samples << " rejected_samples="
                << summary.rejected_samples << " received_reports="
                << drift_report_messages_ << std::endl;
      QCoreApplication::exit(EXIT_SUCCESS);
      return;
    }

    const auto current = movie_timeline_->snapshot();
    if (!current) {
      failDriftScenario(QStringLiteral("timeline-unavailable"));
      return;
    }
    if (event.action == shareme::tools::DriftScenarioAction::pause) {
      if (!movie_timeline_->pause()) {
        failDriftScenario(QStringLiteral("pause-rejected"));
        return;
      }
    } else if (event.action == shareme::tools::DriftScenarioAction::resume) {
      if (!movie_timeline_->resume()) {
        failDriftScenario(QStringLiteral("resume-rejected"));
        return;
      }
    } else {
      const auto end_pts_ms = current->start_pts_ms + current->duration_ms;
      const auto target = shareme::tools::bounded_seek_target(
          current->media_pts_ms, event.seek_delta_ms, current->start_pts_ms,
          end_pts_ms);
      const auto expected_generation = current->generation + 1;
      if (!movie_timeline_->seek(target)) {
        failDriftScenario(QStringLiteral("seek-rejected"));
        return;
      }
      const auto after_seek = movie_timeline_->snapshot();
      if (!after_seek || after_seek->generation != expected_generation) {
        failDriftScenario(QStringLiteral("unexpected-generation"));
        return;
      }
    }
    refreshHostPlayback();
    publishPlaybackState();
  }
#else
  failDriftScenario(QStringLiteral("movie-source-unavailable"));
#endif
}

void RtcDemoController::failDriftScenario(const QString& category) {
  if (drift_scenario_failed_)
    return;
  drift_scenario_failed_ = true;
  drift_scenario_timer_.stop();
  setStatus(QStringLiteral("drift-scenario-failed: ") + category);
  const auto summary = drift_aggregator_.summary();
  std::cout << "RESULT drift-study-v1 status=failed category="
            << category.toStdString() << " accepted_samples="
            << summary.accepted_samples << " rejected_samples="
            << summary.rejected_samples << " received_reports="
            << drift_report_messages_ << std::endl;
  QCoreApplication::exit(EXIT_FAILURE);
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
  if (ended && drift_scenario_ && drift_scenario_started_ &&
      !drift_scenario_->completed()) {
    failDriftScenario(QStringLiteral("media-ended-early"));
    return;
  }
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
  if (report)
    ++drift_report_messages_;
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
  if (drift_metrics_writer_ && drift_capture_enabled_ &&
      (!drift_scenario_ || drift_scenario_started_)) {
    if (pending_drift_samples_.size() >= 64) {
      drift_aggregator_.record_rejection();
    } else {
      const auto capture_time_ms = std::chrono::duration_cast<
          std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      shareme::core::DriftSample sample{
          .capture_time_ms = capture_time_ms,
          .sample_index = drift_sample_index_++,
          .report_sequence = report->sequence,
          .generation = report->generation,
          .host_pts_ms = timeline->media_pts_ms,
          .viewer_pts_ms = static_cast<std::int64_t>(report->rendered_pts_ms),
          .delta_ms = *delta,
          .buffer_ms = report->buffer_ms,
          .playing = timeline->state == shareme::rtc::MovieTimelineState::playing,
          .action = decision.action,
          .phase = drift_scenario_ ? drift_phase_
                                   : shareme::core::DriftPhase::steady,
          .selected_candidate_type = {},
      };
      if (drift_aggregator_.accept(sample)) {
        pending_drift_samples_.push_back(std::move(sample));
        if (pending_drift_samples_.size() == 64)
          flushDriftMetrics();
      }
    }
  }
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
