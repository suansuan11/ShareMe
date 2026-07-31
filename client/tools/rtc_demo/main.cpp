#include "rtc_demo_controller.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QVariant>

#include <cstdlib>
#include <iostream>

namespace {
[[noreturn]] void exit_cli(int code) {
  std::cout.flush();
  std::cerr.flush();
  std::_Exit(code);
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
  parser.addOption(server_option);
  parser.addOption(role_option);
  parser.addOption(room_option);
  if (!parser.parse(app.arguments())) {
    std::cerr << parser.errorText().toStdString() << std::endl;
    exit_cli(2);
  }
  if (parser.isSet(help_option)) {
    std::cout << parser.helpText().toStdString();
    exit_cli(0);
  }

  const auto role_text = parser.value(role_option);
  if (!parser.isSet(server_option) ||
      (role_text != QStringLiteral("host") &&
       role_text != QStringLiteral("viewer")) ||
      (role_text == QStringLiteral("viewer") &&
       !parser.isSet(room_option))) {
    std::cerr << "required: --server URL --role host|viewer "
                 "[--room ROOM]"
              << std::endl;
    exit_cli(2);
  }

  const auto role = role_text == QStringLiteral("host")
                        ? shareme::rtc::SignaledRole::host
                        : shareme::rtc::SignaledRole::viewer;
  RtcDemoController controller(QUrl(parser.value(server_option)), role,
                               parser.value(room_option));
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
