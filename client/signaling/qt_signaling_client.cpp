#include "qt_signaling_client.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

QtSignalingClient::QtSignalingClient(QObject* parent) : QObject(parent) {
  connect(&socket_, &QWebSocket::connected, this, [this] { emit statusChanged(QStringLiteral("connected")); });
  connect(&socket_, &QWebSocket::textMessageReceived, this, &QtSignalingClient::handleText);
  connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { emit failed(QStringLiteral("transport-error")); });
}
void QtSignalingClient::connectTo(const QUrl& url) { url_ = url; QNetworkRequest request{url}; if (!session_.token().empty()) request.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray::fromStdString(session_.token())); socket_.open(request); }
void QtSignalingClient::disconnectFromServer() { socket_.abort(); }
void QtSignalingClient::createRoom() { send(session_.create_room()); }
void QtSignalingClient::joinRoom(const QString& roomId) { send(session_.join_room(roomId.toStdString())); }
void QtSignalingClient::relay(const QString& type, const QByteArray& payload) { if (const auto message = session_.relay(type.toStdString(), payload.toStdString())) send(*message); else emit failed(QStringLiteral("relay-rejected")); }
QString QtSignalingClient::roomId() const { return QString::fromStdString(session_.room_id()); }
bool QtSignalingClient::connected() const noexcept { return socket_.state() == QAbstractSocket::ConnectedState; }
void QtSignalingClient::send(const shareme::signaling::Envelope& envelope) { QJsonObject object{{"version", envelope.version}, {"type", QString::fromStdString(envelope.type)}, {"sequence", static_cast<qint64>(envelope.sequence)}, {"payload", QJsonDocument::fromJson(QByteArray::fromStdString(envelope.payload)).object()}}; if (!envelope.room_id.empty()) object.insert("roomId", QString::fromStdString(envelope.room_id)); socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact))); }
void QtSignalingClient::handleText(const QString& text) { const auto object = QJsonDocument::fromJson(text.toUtf8()).object(); const auto payload = object.value("payload").toObject(); shareme::signaling::Envelope envelope{object.value("version").toInt(), object.value("type").toString().toStdString(), object.value("roomId").toString().toStdString(), static_cast<std::uint64_t>(object.value("sequence").toInteger()), QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString()}; if (!session_.handle(envelope)) { emit failed(QStringLiteral("protocol-error")); return; } if (envelope.type == "room-created" || envelope.type == "room-joined") emit roomReady(roomId()); else if (envelope.type == "error") emit failed(payload.value("code").toString()); else emit relayReceived(QString::fromStdString(envelope.type), QJsonDocument(payload).toJson(QJsonDocument::Compact)); }
