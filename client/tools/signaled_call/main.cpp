#include "qt_signaling_client.hpp"
#include "shareme/rtc/signaled_peer.hpp"

#ifdef SHAREME_HAS_MOVIE_RTC
#include "shareme/rtc/movie_audio_source.hpp"
#include "shareme/rtc/movie_timeline.hpp"
#include "shareme/rtc/movie_video_source.hpp"
#endif

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTimer>

#include <chrono>
#include <filesystem>
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
  QCommandLineOption video_option(QStringList{"video"}, "synthetic or movie",
                                  "mode", "synthetic");
  QCommandLineOption movie_option(QStringList{"movie"}, "Host movie path",
                                  "path");
  QCommandLineOption movie_audio_option(QStringList{"movie-audio"},
                                        "Send the host movie audio track");
  QCommandLineOption room_option(QStringList{"room"}, "Room ID for viewer",
                                 "room");
  parser.addOption(server);
  parser.addOption(role_option);
  parser.addOption(audio_option);
  parser.addOption(video_option);
  parser.addOption(movie_option);
  parser.addOption(movie_audio_option);
  parser.addOption(room_option);
  parser.process(app);
  const auto role_text = parser.value(role_option);
  const auto audio_text = parser.value(audio_option);
  const auto video_text = parser.value(video_option);
  const auto movie_is_set = parser.isSet(movie_option);
  const auto movie_audio_is_set = parser.isSet(movie_audio_option);
  if (!parser.isSet(server) || (role_text != "host" && role_text != "viewer") ||
      (audio_text != "synthetic" && audio_text != "microphone") ||
      (video_text != "synthetic" && video_text != "movie") ||
      (video_text == "movie" && (role_text != "host" || !movie_is_set)) ||
      (video_text != "movie" && movie_is_set) ||
      (movie_audio_is_set &&
       (role_text != "host" || video_text != "movie" || !movie_is_set)) ||
      (role_text == "viewer" && !parser.isSet(room_option)))
    return 2;
#ifndef SHAREME_HAS_MOVIE_RTC
  if (movie_audio_is_set) {
    std::cerr << "PEER_ERROR movie-audio-dependency-unavailable" << std::endl;
    return 1;
  }
  if (video_text == "movie") {
    std::cerr << "PEER_ERROR movie-video-dependency-unavailable" << std::endl;
    return 1;
  }
#endif
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
                      << " width=" << result.video_width
                      << " height=" << result.video_height
                      << " audio_sent=" << result.audio_packets_sent
                      << " audio_received=" << result.audio_packets_received
                      << " audio_level="
                      << result.local_audio_level.value_or(0.0)
                      << " movie_audio_frames_received="
                      << result.movie_audio_frames_received
                      << " sample_rate=" << result.movie_audio_sample_rate
                      << " channels=" << result.movie_audio_channels
                      << " peak=" << result.movie_audio_peak
                      << " chunks_generated="
                      << result.movie_audio_chunks_generated
                      << " movie_av_skew_ms="
                      << result.movie_av_skew_ms.value_or(0)
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
          Qt::AutoConnection);
    };
    shareme::rtc::SignaledPeerConfig config{.role = role,
                                            .audio_mode = audio_mode};
#ifdef SHAREME_HAS_MOVIE_RTC
    if (video_text == "movie") {
      const std::filesystem::path movie_path{
          parser.value(movie_option).toStdString()};
      config.video_mode = shareme::rtc::SignaledVideoMode::injected;
      if (movie_audio_is_set) {
        auto timeline = std::make_shared<shareme::rtc::MovieTimeline>();
        config.video_source_factory = [movie_path,
                                       timeline](webrtc::TaskQueueFactory &) {
          return shareme::rtc::MovieVideoSource::create(movie_path, timeline);
        };
        config.movie_audio_source_factory = [movie_path, timeline] {
          return shareme::rtc::MovieAudioSource::create(movie_path, timeline);
        };
      } else {
        config.video_source_factory = [movie_path](webrtc::TaskQueueFactory &) {
          return shareme::rtc::MovieVideoSource::create(movie_path);
        };
      }
    }
#endif
    peer = shareme::rtc::SignaledPeer::create(std::move(config),
                                              std::move(callbacks));
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
  if (peer && waiter.joinable())
    peer->cancel_wait();
  if (waiter.joinable())
    waiter.join();
  if (peer)
    peer->stop();
  return exit_code;
}
