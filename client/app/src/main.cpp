#include "playback_controller.hpp"

#include <QGuiApplication>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <cstdlib>

int main(int argc, char* argv[]) {
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QGuiApplication application{argc, argv};
  QCoreApplication::setApplicationName(QStringLiteral("ShareMe Playback"));
  QCoreApplication::setOrganizationName(QStringLiteral("ShareMe"));
#ifdef _WIN32
  application.setFont(QFont{QStringLiteral("Bahnschrift")});
#else
  application.setFont(QFont{QStringLiteral("Avenir Next")});
#endif

  PlaybackController playback;
  QQmlApplicationEngine engine;
  engine.setInitialProperties(
      {{QStringLiteral("playback"), QVariant::fromValue(&playback)}});
  engine.loadFromModule(QStringLiteral("ShareMe"), QStringLiteral("Main"));
  if (engine.rootObjects().isEmpty()) {
    return EXIT_FAILURE;
  }

  if (application.arguments().contains(QStringLiteral("--smoke-test"))) {
    QTimer::singleShot(250, &application, &QCoreApplication::quit);
  }

  const auto media_smoke_index =
      application.arguments().indexOf(QStringLiteral("--media-smoke"));
  if (media_smoke_index >= 0) {
    if (media_smoke_index + 1 >= application.arguments().size()) {
      return EXIT_FAILURE;
    }

    const auto movie_path = application.arguments().at(media_smoke_index + 1);
    QTimer::singleShot(0, &application, [&playback, movie_path] {
      playback.open(QUrl::fromLocalFile(movie_path));
      playback.play();
    });
    QTimer::singleShot(2'500, &application, [&application, &playback] {
      application.exit(playback.state() == QStringLiteral("ended")
                           ? EXIT_SUCCESS
                           : EXIT_FAILURE);
    });
  }

  return application.exec();
}
