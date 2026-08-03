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
  QCommandLineOption validate_option(QStringList{QStringLiteral("validate")},
                                     QStringLiteral("Validate configuration and exit"));
  parser.addOption(server_option);
  parser.addOption(role_option);
  parser.addOption(room_option);
  parser.addOption(source_option);
  parser.addOption(movie_option);
  parser.addOption(movie_audio_option);
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
      (parser.isSet(movie_audio_option) && source_text != QStringLiteral("movie"))) {
    std::cerr << "required: --server URL --role host|viewer "
                 "[--room ROOM] [--source test|desktop|movie] [--movie PATH] [--movie-audio]"
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
                               local_path(parser.value(movie_option)),
                               parser.isSet(movie_audio_option));
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
