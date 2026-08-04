#include "rtc_demo_controller.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QVariant>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {
[[noreturn]] void exit_cli(int code) {
  std::cout.flush();
  std::cerr.flush();
  std::_Exit(code);
}

[[nodiscard]] std::filesystem::path local_path(const QString &path) {
#ifdef _WIN32
  return std::filesystem::path{path.toStdWString()};
#else
  return std::filesystem::path{path.toStdString()};
#endif
}
} // namespace

int main(int argc, char **argv) {
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("ShareMe RTC Demo"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Minimal ShareMe WebRTC sender/receiver demo"));
  const auto help_option = parser.addHelpOption();
  QCommandLineOption server_option(QStringList{QStringLiteral("s"),
                                               QStringLiteral("server")},
                                   QStringLiteral("WebSocket URL"),
                                   QStringLiteral("url"));
  QCommandLineOption role_option(QStringList{QStringLiteral("r"),
                                             QStringLiteral("role")},
                                 QStringLiteral("host or viewer"),
                                 QStringLiteral("role"));
  QCommandLineOption room_option(QStringList{QStringLiteral("room")},
                                 QStringLiteral("Room ID for viewer"),
                                 QStringLiteral("room"));
  QCommandLineOption source_option(QStringList{QStringLiteral("source")},
                                   QStringLiteral("test, desktop, or movie"),
                                   QStringLiteral("source"),
                                   QStringLiteral("test"));
  QCommandLineOption movie_option(QStringList{QStringLiteral("movie")},
                                  QStringLiteral("Movie path for a movie host"),
                                  QStringLiteral("path"));
  QCommandLineOption movie_audio_option(QStringList{QStringLiteral("movie-audio")},
                                        QStringLiteral("Send independent movie audio"));
  QCommandLineOption video_acceleration_option(
      QStringList{QStringLiteral("video-acceleration")},
      QStringLiteral("Movie video path: auto or software"),
      QStringLiteral("mode"), QStringLiteral("auto"));
  QCommandLineOption metrics_option(
      QStringList{QStringLiteral("metrics-jsonl")},
      QStringLiteral("Host-only sanitized drift JSONL output"),
      QStringLiteral("path"));
  QCommandLineOption scenario_option(
      QStringList{QStringLiteral("drift-scenario")},
      QStringLiteral("Host-only scripted drift profile (drift-study-v1)"),
      QStringLiteral("name"));
  QCommandLineOption duration_option(
      QStringList{QStringLiteral("measurement-duration-seconds")},
      QStringLiteral("Frozen drift-study duration in seconds (300)"),
      QStringLiteral("seconds"));
  QCommandLineOption validate_option(QStringList{QStringLiteral("validate")},
                                     QStringLiteral("Validate configuration and exit"));
  parser.addOption(server_option);
  parser.addOption(role_option);
  parser.addOption(room_option);
  parser.addOption(source_option);
  parser.addOption(movie_option);
  parser.addOption(movie_audio_option);
  parser.addOption(video_acceleration_option);
  parser.addOption(metrics_option);
  parser.addOption(scenario_option);
  parser.addOption(duration_option);
  parser.addOption(validate_option);
  if (!parser.parse(app.arguments())) {
    std::cerr << parser.errorText().toStdString() << std::endl;
    exit_cli(2);
  }
  if (parser.isSet(help_option)) {
    std::cout << parser.helpText().toStdString();
    exit_cli(0);
  }

  const auto role_text = parser.value(role_option);
  const auto source_text = parser.value(source_option);
  const auto movie_path = local_path(parser.value(movie_option));
  const auto video_acceleration = parser.value(video_acceleration_option);
  const auto metrics_path = local_path(parser.value(metrics_option));
  bool duration_ok = false;
  const auto duration_seconds =
      parser.value(duration_option).toLongLong(&duration_ok);
  auto normalized_path = [](const std::filesystem::path &path) {
    std::error_code error;
    return std::filesystem::absolute(path, error).lexically_normal();
  };
  const auto metrics_matches_movie =
      parser.isSet(metrics_option) && parser.isSet(movie_option) &&
      !metrics_path.empty() && !movie_path.empty() &&
      normalized_path(metrics_path) == normalized_path(movie_path);
  const auto scenario_set = parser.isSet(scenario_option);
  const auto duration_set = parser.isSet(duration_option);
  const auto scenario_valid =
      (!scenario_set && !duration_set) ||
      (scenario_set && duration_set && role_text == QStringLiteral("host") &&
       source_text == QStringLiteral("movie") &&
       parser.value(scenario_option) == QStringLiteral("drift-study-v1") &&
       duration_ok && duration_seconds == 300);
  if (!parser.isSet(server_option) ||
      (role_text != QStringLiteral("host") &&
       role_text != QStringLiteral("viewer")) ||
      (role_text == QStringLiteral("viewer") &&
       !parser.isSet(room_option)) ||
      (source_text != QStringLiteral("test") && source_text != QStringLiteral("desktop") &&
       source_text != QStringLiteral("movie")) ||
      (role_text == QStringLiteral("viewer") &&
       source_text != QStringLiteral("test")) ||
      (source_text == QStringLiteral("movie") && !parser.isSet(movie_option)) ||
      (source_text != QStringLiteral("movie") && parser.isSet(movie_option)) ||
      ((parser.isSet(video_acceleration_option) ||
        video_acceleration != QStringLiteral("auto")) &&
       (source_text != QStringLiteral("movie") ||
        role_text != QStringLiteral("host") ||
        (video_acceleration != QStringLiteral("auto") &&
         video_acceleration != QStringLiteral("software")))) ||
      (parser.isSet(movie_audio_option) && source_text != QStringLiteral("movie")) ||
      (parser.isSet(metrics_option) &&
       (role_text != QStringLiteral("host") ||
        source_text != QStringLiteral("movie") || metrics_path.empty() ||
        metrics_matches_movie)) ||
      !scenario_valid) {
    std::cerr << "required: --server URL --role host|viewer "
                 "[--room ROOM] [--source test|desktop|movie] [--movie PATH] [--movie-audio] "
                 "[--video-acceleration auto|software] "
                 "[--metrics-jsonl PATH] [--drift-scenario drift-study-v1 "
                 "--measurement-duration-seconds 300]"
              << std::endl;
    exit_cli(2);
  }
#if !defined(SHAREME_HAS_DESKTOP_CAPTURE)
  if (source_text == QStringLiteral("desktop")) {
    std::cerr << "desktop source is only available on Windows" << std::endl;
    exit_cli(2);
  }
#endif
#if !defined(SHAREME_HAS_MOVIE_RTC)
  if (source_text == QStringLiteral("movie")) {
    std::cerr << "movie source requires an FFmpeg-enabled build" << std::endl;
    exit_cli(2);
  }
#endif
  if (parser.isSet(validate_option))
    exit_cli(0);

  const auto role = role_text == QStringLiteral("host")
                        ? shareme::rtc::SignaledRole::host
                        : shareme::rtc::SignaledRole::viewer;
  RtcDemoController controller(QUrl(parser.value(server_option)), role,
                               parser.value(room_option),
                               source_text == QStringLiteral("desktop"),
                               movie_path, parser.isSet(movie_audio_option),
                               video_acceleration,
                               parser.value(metrics_option),
                               parser.value(scenario_option), duration_seconds);
  QQmlApplicationEngine engine;
  engine.setInitialProperties(
      {{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule(QStringLiteral("ShareMe.RtcDemo"),
                        QStringLiteral("Main"));
  if (engine.rootObjects().isEmpty())
    return EXIT_FAILURE;
  QMetaObject::invokeMethod(&controller, &RtcDemoController::start,
                            Qt::QueuedConnection);
  return app.exec();
}
