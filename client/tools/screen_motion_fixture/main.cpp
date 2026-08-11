#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>

#include <iostream>

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  QCommandLineOption profile_option(
      QStringList{QStringLiteral("profile")},
      QStringLiteral("Motion profile: standard, quality, or cinema"),
      QStringLiteral("profile"));
  QCommandLineOption duration_option(
      QStringList{QStringLiteral("duration-seconds")},
      QStringLiteral("Bounded fixture duration"), QStringLiteral("seconds"));
  parser.addOption(profile_option);
  parser.addOption(duration_option);
  parser.process(app);

  bool duration_ok = false;
  const int duration_seconds = parser.value(duration_option).toInt(&duration_ok);
  const QString profile = parser.value(profile_option);
  if (!parser.isSet(profile_option) ||
      (profile != QStringLiteral("standard") &&
       profile != QStringLiteral("quality") &&
       profile != QStringLiteral("cinema")) ||
      !parser.isSet(duration_option) || !duration_ok || duration_seconds < 1 ||
      duration_seconds > 3600) {
    std::cerr << "invalid fixture arguments" << std::endl;
    return 2;
  }

  const int frames_per_second =
      profile == QStringLiteral("cinema") ? 30 : 60;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("fixtureProfile"),
                                           profile);
  engine.rootContext()->setContextProperty(QStringLiteral("fixtureFps"),
                                           frames_per_second);
  engine.loadFromModule("ShareMe.ScreenMotionFixture", "Main");
  if (engine.rootObjects().isEmpty())
    return 1;

  QObject *root = engine.rootObjects().constFirst();
  QTimer::singleShot(duration_seconds * 1000, &app, &QCoreApplication::quit);
  QObject::connect(&app, &QCoreApplication::aboutToQuit, [root, profile] {
    std::cout << "SCREEN_MOTION_FIXTURE status=completed profile="
              << profile.toStdString()
              << " frames=" << root->property("renderedFrames").toInt()
              << std::endl;
  });
  return app.exec();
}
