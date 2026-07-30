#include "qt_signaling_client.hpp"
#include "shareme/rtc/signaled_peer.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTimer>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

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

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  QCommandLineOption server(QStringList{"s", "server"}, "WebSocket URL", "url");
  QCommandLineOption role_option(QStringList{"r", "role"}, "host or viewer",
                                 "role");
  QCommandLineOption audio_option(
      QStringList{"audio"}, "synthetic or microphone", "mode", "synthetic");
  QCommandLineOption room_option(QStringList{"room"}, "Room ID for viewer",
                                 "room");
  parser.addOption(server);
  parser.addOption(role_option);
  parser.addOption(audio_option);
  parser.addOption(room_option);
  parser.process(app);
  const auto role_text = parser.value(role_option);
  const auto audio_text = parser.value(audio_option);
  if (!parser.isSet(server) || (role_text != "host" && role_text != "viewer") ||
      (audio_text != "synthetic" && audio_text != "microphone") ||
      (role_text == "viewer" && !parser.isSet(room_option)))
    return 2;
  const auto role = role_text == "host" ? shareme::rtc::SignaledRole::host
                                        : shareme::rtc::SignaledRole::viewer;
  const auto audio_mode = audio_text == "microphone"
                              ? shareme::rtc::SignaledAudioMode::microphone
                              : shareme::rtc::SignaledAudioMode::synthetic;
  QtSignalingClient client;
  std::unique_ptr<shareme::rtc::SignaledPeer> peer;
  std::jthread waiter;
  bool started = false;
  int exit_code = 1;
  auto start_peer = [&] {
    if (started || !peer)
      return;
    started = peer->start();
    if (!started) {
      app.quit();
      return;
    }
    waiter = std::jthread([&] {
      const auto result = peer->wait(std::chrono::seconds(15));
      QMetaObject::invokeMethod(
          &app,
          [&, result] {
            std::cout << "RESULT connected=" << (result.connected ? 1 : 0)
                      << " video=" << result.video_frames_received
                      << " audio_sent=" << result.audio_packets_sent
                      << " audio_received=" << result.audio_packets_received
                      << " audio_level="
                      << result.local_audio_level.value_or(0.0)
                      << " candidate=" << result.selected_candidate_type
                      << " error=" << result.error << std::endl;
            exit_code = result.error.empty() ? 0 : 1;
            app.quit();
          },
          Qt::QueuedConnection);
    });
  };
  auto create_peer = [&] {
    if (peer)
      return true;
    shareme::rtc::SignaledPeerCallbacks callbacks;
    callbacks.description = [&](std::string type, std::string sdp) {
      QMetaObject::invokeMethod(
          &client,
          [&, type = std::move(type), sdp = std::move(sdp)] {
            client.relay(QStringLiteral("session-description"),
                         description_payload(type, sdp));
          },
          Qt::QueuedConnection);
    };
    callbacks.candidate = [&](std::string mid, int line,
                              std::string candidate) {
      QMetaObject::invokeMethod(
          &client,
          [&, mid = std::move(mid), line, candidate = std::move(candidate)] {
            client.relay(QStringLiteral("ice-candidate"),
                         candidate_payload(mid, line, candidate));
          },
          Qt::QueuedConnection);
    };
    callbacks.failure = [&](std::string category) {
      QMetaObject::invokeMethod(
          &app,
          [&, category = std::move(category)] {
            std::cerr << "PEER_ERROR " << category << std::endl;
            exit_code = 1;
            app.quit();
          },
          Qt::QueuedConnection);
    };
    peer = shareme::rtc::SignaledPeer::create(
        {.role = role, .audio_mode = audio_mode}, std::move(callbacks));
    return peer != nullptr;
  };
  QObject::connect(&client, &QtSignalingClient::statusChanged, &app,
                   [&](const QString &state) {
                     if (state != "connected")
                       return;
                     if (role == shareme::rtc::SignaledRole::host)
                       client.createRoom();
                     else
                       client.joinRoom(parser.value(room_option));
                   });
  QObject::connect(&client, &QtSignalingClient::roomReady, &app,
                   [&](const QString &room) {
                     if (!create_peer()) {
                       app.exit(1);
                       return;
                     }
                     if (role == shareme::rtc::SignaledRole::host)
                       std::cout << "ROOM " << room.toStdString() << std::endl;
                     else
                       start_peer();
                   });
  QObject::connect(
      &client, &QtSignalingClient::relayReceived, &app,
      [&](const QString &type, const QByteArray &raw) {
        if (type == "participant-joined" &&
            role == shareme::rtc::SignaledRole::host) {
          start_peer();
          return;
        }
        if (!peer)
          return;
        const auto object = QJsonDocument::fromJson(raw).object();
        if (type == "session-description")
          static_cast<void>(peer->receive_description(
              object.value("descriptionType").toString().toStdString(),
              object.value("sdp").toString().toStdString()));
        else if (type == "ice-candidate")
          static_cast<void>(peer->receive_candidate(
              object.value("sdpMid").toString().toStdString(),
              object.value("sdpMLineIndex").toInt(),
              object.value("candidate").toString().toStdString()));
      });
  QObject::connect(
      &client, &QtSignalingClient::failed, &app, [&](const QString &code) {
        std::cerr << "SIGNALING_ERROR " << code.toStdString() << std::endl;
        exit_code = 1;
        app.quit();
      });
  QTimer::singleShot(20000, &app, [&] {
    exit_code = 1;
    app.quit();
  });
  client.connectTo(QUrl(parser.value(server)));
  static_cast<void>(app.exec());
  if (peer)
    peer->stop();
  return exit_code;
}
