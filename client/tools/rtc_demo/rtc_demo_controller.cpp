#include "rtc_demo_controller.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QVideoFrame>
#include <QVideoSink>

#include <chrono>
#include <iostream>
#include <utility>

#include "libyuv/convert_argb.h"

#if defined(SHAREME_HAS_DESKTOP_CAPTURE)
#include "shareme/rtc/desktop_capture_source.hpp"
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

} // namespace

RtcDemoController::RtcDemoController(QUrl server_url,
                                     shareme::rtc::SignaledRole role,
                                     QString requested_room,
                                     bool desktop_source, QObject *parent)
    : QObject(parent), server_url_(std::move(server_url)), role_(role),
      requested_room_(std::move(requested_room)),
      desktop_source_(desktop_source) {
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
            if (!peer_)
              return;
            const auto object = QJsonDocument::fromJson(raw).object();
            if (type == QStringLiteral("session-description")) {
              if (!peer_->receive_description(
                      object.value("descriptionType").toString().toStdString(),
                      object.value("sdp").toString().toStdString()))
                setStatus(QStringLiteral("remote-description-rejected"));
            } else if (type == QStringLiteral("ice-candidate")) {
              if (!peer_->receive_candidate(
                      object.value("sdpMid").toString().toStdString(),
                      object.value("sdpMLineIndex").toInt(),
                      object.value("candidate").toString().toStdString()))
                setStatus(QStringLiteral("remote-candidate-rejected"));
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

void RtcDemoController::setVideoSink(QVideoSink *sink) { video_sink_ = sink; }

void RtcDemoController::start() {
  if (start_requested_)
    return;
  start_requested_ = true;
  setStatus(QStringLiteral("connecting"));
  signaling_.connectTo(server_url_);
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
  config.remote_video_frame =
      [this](const webrtc::VideoFrame &frame) { deliverRemoteFrame(frame); };
  peer_ = shareme::rtc::SignaledPeer::create(std::move(config),
                                             std::move(callbacks));
  if (!peer_) {
    setStatus(QStringLiteral("peer-creation-failed"));
    return false;
  }
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
  setStatus(QStringLiteral("negotiating"));
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
  if (peer_)
    peer_->cancel_wait();
  if (waiter_.joinable())
    waiter_.join();
  if (peer_)
    peer_->stop();
  peer_.reset();
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
      [this, image = std::move(image)] {
        if (video_sink_)
          video_sink_->setVideoFrame(QVideoFrame(image));
        video_delivery_pending_.store(false, std::memory_order_release);
      },
      Qt::QueuedConnection);
  if (!queued)
    release_delivery();
}
