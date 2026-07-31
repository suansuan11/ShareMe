#pragma once

#include "qt_signaling_client.hpp"
#include "shareme/rtc/signaled_peer.hpp"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVideoSink>

#include <memory>
#include <thread>

class RtcDemoController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString roomId READ roomId NOTIFY roomIdChanged)
  Q_PROPERTY(bool viewer READ viewer CONSTANT)

public:
  RtcDemoController(QUrl server_url, shareme::rtc::SignaledRole role,
                    QString requested_room, QObject *parent = nullptr);
  ~RtcDemoController() override;

  RtcDemoController(const RtcDemoController &) = delete;
  RtcDemoController &operator=(const RtcDemoController &) = delete;

  [[nodiscard]] QString status() const;
  [[nodiscard]] QString roomId() const;
  [[nodiscard]] bool viewer() const noexcept;

  Q_INVOKABLE void setVideoSink(QVideoSink *sink);
  Q_INVOKABLE void start();

signals:
  void statusChanged();
  void roomIdChanged();

private:
  bool createPeer();
  void startPeer();
  void stopPeer() noexcept;
  void setStatus(QString status);
  void setRoomId(QString room_id);
  void deliverRemoteFrame(const webrtc::VideoFrame &frame);

  QUrl server_url_;
  shareme::rtc::SignaledRole role_;
  QString requested_room_;
  QString status_{QStringLiteral("idle")};
  QString room_id_;
  QPointer<QVideoSink> video_sink_;
  QtSignalingClient signaling_;
  std::unique_ptr<shareme::rtc::SignaledPeer> peer_;
  std::jthread waiter_;
  bool peer_started_{false};
  bool start_requested_{false};
};
