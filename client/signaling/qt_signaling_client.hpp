#pragma once

#include "shareme/signaling/signaling_session.hpp"

#include <QObject>
#include <QUrl>
#include <QWebSocket>

class QtSignalingClient final : public QObject {
  Q_OBJECT
public:
  explicit QtSignalingClient(QObject* parent = nullptr);
  void connectTo(const QUrl& url);
  void createRoom();
  void joinRoom(const QString& roomId);
  void relay(const QString& type, const QByteArray& payload);
  [[nodiscard]] QString roomId() const;
signals:
  void statusChanged(const QString& status);
  void roomReady(const QString& roomId);
  void relayReceived(const QString& type, const QByteArray& payload);
  void failed(const QString& code);
private:
  void send(const shareme::signaling::Envelope& envelope);
  void handleText(const QString& text);
  QWebSocket socket_;
  QUrl url_;
  shareme::signaling::SignalingSession session_;
};
