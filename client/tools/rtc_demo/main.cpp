#include "rtc_demo_controller.hpp"
#include "launch_options.hpp"
#include "shareme_app_controller.hpp"
#include "shareme/core/screen_stream_profile.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QTimer>
#include <QVariant>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <array>

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
  QCoreApplication::setApplicationName(QStringLiteral("ShareMe"));
  QGuiApplication::setApplicationDisplayName(QStringLiteral("ShareMe"));
  QCoreApplication::setOrganizationName(QStringLiteral("ShareMe"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("ShareMe screen-sharing calls"));
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
                                    QStringLiteral(
                                        "test, desktop, movie, or screen"),
                                    QStringLiteral("source"),
                                    QString());
  QCommandLineOption screen_profile_option(
      QStringList{QStringLiteral("screen-profile")},
      QStringLiteral("Screen profile: standard, quality, or cinema"),
      QStringLiteral("profile"), QStringLiteral("standard"));
  QCommandLineOption screen_encoder_option(
      QStringList{QStringLiteral("screen-encoder")},
      QStringLiteral("Screen encoder: auto or software"),
      QStringLiteral("mode"), QStringLiteral("auto"));
  QCommandLineOption audio_option(
      QStringList{QStringLiteral("audio")},
      QStringLiteral("Primary voice source: microphone or synthetic"),
      QStringLiteral("mode"), QStringLiteral("microphone"));
  QCommandLineOption no_audio_playout_option(
      QStringList{QStringLiteral("no-audio-playout")},
      QStringLiteral("Disable native primary voice playout"));
  QCommandLineOption movie_option(QStringList{QStringLiteral("movie")},
                                  QStringLiteral("Movie path for a movie host"),
                                  QStringLiteral("path"));
  QCommandLineOption movie_audio_option(QStringList{QStringLiteral("movie-audio")},
                                        QStringLiteral("Send independent movie audio"));
  QCommandLineOption video_acceleration_option(
      QStringList{QStringLiteral("video-acceleration")},
      QStringLiteral("Movie video path: auto or software"),
      QStringLiteral("mode"), QStringLiteral("software"));
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
  QCommandLineOption gui_smoke_state_option(
      QStringList{QStringLiteral("gui-smoke-state")},
      QStringLiteral("Run a bounded GUI state smoke and exit"),
      QStringLiteral("home|create|join"));
  parser.addOption(server_option);
  parser.addOption(role_option);
  parser.addOption(room_option);
  parser.addOption(source_option);
  parser.addOption(screen_profile_option);
  parser.addOption(screen_encoder_option);
  parser.addOption(audio_option);
  parser.addOption(no_audio_playout_option);
  parser.addOption(movie_option);
  parser.addOption(movie_audio_option);
  parser.addOption(video_acceleration_option);
  parser.addOption(metrics_option);
  parser.addOption(scenario_option);
  parser.addOption(duration_option);
  parser.addOption(validate_option);
  parser.addOption(gui_smoke_state_option);
  if (!parser.parse(app.arguments())) {
    std::cerr << parser.errorText().toStdString() << std::endl;
    exit_cli(2);
  }
  if (parser.isSet(help_option)) {
    std::cout << parser.helpText().toStdString();
    exit_cli(0);
  }

  const auto gui_smoke_state = parser.value(gui_smoke_state_option);
  if (parser.isSet(gui_smoke_state_option) &&
      gui_smoke_state != QStringLiteral("home") &&
      gui_smoke_state != QStringLiteral("create") &&
      gui_smoke_state != QStringLiteral("join") &&
      gui_smoke_state != QStringLiteral("call-host") &&
      gui_smoke_state != QStringLiteral("call-viewer") &&
      gui_smoke_state != QStringLiteral("call-host-actions")) {
    std::cerr << "invalid --gui-smoke-state" << std::endl;
    exit_cli(2);
  }

  const auto role_text = parser.value(role_option);
  const auto any_rtc_option =
      parser.isSet(server_option) || parser.isSet(role_option) ||
      parser.isSet(room_option) || parser.isSet(source_option) ||
      parser.isSet(screen_profile_option) ||
      parser.isSet(screen_encoder_option) || parser.isSet(audio_option) ||
      parser.isSet(no_audio_playout_option) || parser.isSet(movie_option) ||
      parser.isSet(movie_audio_option) ||
      parser.isSet(video_acceleration_option) || parser.isSet(metrics_option) ||
      parser.isSet(scenario_option) || parser.isSet(duration_option) ||
      parser.isSet(validate_option);
  const auto launch = shareme::tools::classify_launch(
      any_rtc_option, parser.isSet(server_option), role_text);
  auto source_text = parser.value(source_option);
  if (source_text.isEmpty()) {
#if defined(__APPLE__)
    source_text = role_text == QStringLiteral("host")
                      ? QStringLiteral("screen")
                      : QStringLiteral("test");
#else
    source_text = QStringLiteral("test");
#endif
  }
  const auto screen_profile_value = parser.value(screen_profile_option);
  const auto screen_profile = shareme::core::parse_screen_stream_profile(
      screen_profile_value.toStdString());
  const auto screen_encoder_value = parser.value(screen_encoder_option);
  const auto audio_value = parser.value(audio_option);
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
  if (!launch.accepted ||
      (!launch.interactive && (!parser.isSet(server_option) ||
      (role_text != QStringLiteral("host") &&
       role_text != QStringLiteral("viewer")) ||
      (role_text == QStringLiteral("viewer") &&
       !parser.isSet(room_option)) ||
       (source_text != QStringLiteral("test") &&
        source_text != QStringLiteral("desktop") &&
        source_text != QStringLiteral("movie") &&
        source_text != QStringLiteral("screen")) ||
       (role_text == QStringLiteral("viewer") &&
        source_text != QStringLiteral("test") &&
        source_text != QStringLiteral("screen")) ||
       (source_text == QStringLiteral("movie") &&
        !parser.isSet(movie_option)) ||
       (source_text != QStringLiteral("movie") &&
        parser.isSet(movie_option)) ||
       !screen_profile.has_value() ||
       (audio_value != QStringLiteral("microphone") &&
        audio_value != QStringLiteral("synthetic")) ||
       (parser.isSet(screen_profile_option) &&
        source_text != QStringLiteral("screen")) ||
       (screen_encoder_value != QStringLiteral("auto") &&
        screen_encoder_value != QStringLiteral("software")) ||
       (parser.isSet(screen_encoder_option) &&
        (source_text != QStringLiteral("screen") ||
         role_text != QStringLiteral("host"))) ||
       (screen_encoder_value == QStringLiteral("software") &&
        screen_profile != shareme::core::ScreenStreamProfile::standard) ||
       (video_acceleration != QStringLiteral("auto") &&
        video_acceleration != QStringLiteral("software")) ||
       (parser.isSet(video_acceleration_option) &&
        (source_text != QStringLiteral("movie") ||
         role_text != QStringLiteral("host"))) ||
       (parser.isSet(movie_audio_option) &&
        source_text != QStringLiteral("movie")) ||
       (parser.isSet(metrics_option) &&
        (role_text != QStringLiteral("host") ||
         source_text != QStringLiteral("movie") || metrics_path.empty() ||
         metrics_matches_movie)) ||
      !scenario_valid))) {
    std::cerr << "required: --server URL --role host|viewer "
                 "[--room ROOM] [--source test|desktop|movie|screen] "
                 "[--screen-profile standard|quality|cinema] [--movie PATH] "
                 "[--screen-encoder auto|software] "
                 "[--audio microphone|synthetic] [--no-audio-playout] "
                 "[--movie-audio] [--video-acceleration auto|software] "
                 "[--metrics-jsonl PATH] [--drift-scenario drift-study-v1 "
                 "--measurement-duration-seconds 300]"
              << std::endl;
    exit_cli(2);
  }
#if !defined(SHAREME_HAS_DESKTOP_CAPTURE)
  if (!launch.interactive &&
      source_text == QStringLiteral("desktop")) {
    std::cerr << "desktop source is only available on Windows" << std::endl;
    exit_cli(2);
  }
#endif
#if !defined(__APPLE__) && !defined(SHAREME_HAS_DESKTOP_CAPTURE)
  if (!launch.interactive && source_text == QStringLiteral("screen")) {
    std::cerr << "screen source requires macOS or Windows Desktop Duplication"
              << std::endl;
    exit_cli(2);
  }
#endif
#if !defined(SHAREME_HAS_MOVIE_RTC)
  if (!launch.interactive && source_text == QStringLiteral("movie")) {
    std::cerr << "movie source requires an FFmpeg-enabled build" << std::endl;
    exit_cli(2);
  }
#endif
  if (parser.isSet(validate_option))
    exit_cli(0);

  const auto factory = [](const shareme::tools::AppSessionConfig &config,
                          QObject *parent)
      -> std::unique_ptr<shareme::tools::CallSession> {
    const auto role = config.role == shareme::tools::InteractiveRole::host
                          ? shareme::rtc::SignaledRole::host
                          : shareme::rtc::SignaledRole::viewer;
    auto controller = std::make_unique<RtcDemoController>(
        config.server_url, role, config.requested_room,
        config.video_source == shareme::tools::SessionVideoSource::desktop,
        config.video_source == shareme::tools::SessionVideoSource::screen,
        config.screen_profile, config.audio_mode, config.native_audio_playout,
        config.movie_path, config.movie_audio, config.video_acceleration,
        config.metrics_jsonl_path, config.drift_scenario_name,
        config.measurement_duration_seconds, parent);
    if (config.screen_encoder == shareme::tools::ScreenEncoderMode::software)
      controller->setScreenEncoderMode(config.screen_encoder);
    return controller;
  };
  QSettings settings;
  shareme::tools::AppPreferences preferences(settings);
  shareme::tools::ShareMeAppController app_controller(factory, &preferences);
  QQmlApplicationEngine engine;
  engine.setInitialProperties(
      {{QStringLiteral("appController"),
        QVariant::fromValue(&app_controller)}});
  engine.loadFromModule(QStringLiteral("ShareMe.RtcDemo"),
                        QStringLiteral("Main"));
  if (engine.rootObjects().isEmpty())
    return EXIT_FAILURE;
  if (!launch.interactive) {
    shareme::tools::AppSessionConfig config;
    config.server_url = QUrl(parser.value(server_option));
    config.role = role_text == QStringLiteral("host")
                      ? shareme::tools::InteractiveRole::host
                      : shareme::tools::InteractiveRole::viewer;
    config.requested_room = parser.value(room_option);
    if (source_text == QStringLiteral("desktop"))
      config.video_source = shareme::tools::SessionVideoSource::desktop;
    else if (source_text == QStringLiteral("movie"))
      config.video_source = shareme::tools::SessionVideoSource::movie;
    else if (source_text == QStringLiteral("screen"))
      config.video_source = shareme::tools::SessionVideoSource::screen;
    else
      config.video_source = shareme::tools::SessionVideoSource::test;
    config.screen_profile = *screen_profile;
    config.screen_encoder =
        screen_encoder_value == QStringLiteral("software")
            ? shareme::tools::ScreenEncoderMode::software
            : shareme::tools::ScreenEncoderMode::auto_mode;
    config.audio_mode = audio_value == QStringLiteral("microphone")
                            ? shareme::rtc::SignaledAudioMode::microphone
                            : shareme::rtc::SignaledAudioMode::synthetic;
    config.native_audio_playout = !parser.isSet(no_audio_playout_option);
    config.movie_path = movie_path;
    config.movie_audio = parser.isSet(movie_audio_option);
    config.video_acceleration = video_acceleration;
    config.metrics_jsonl_path = parser.value(metrics_option);
    config.drift_scenario_name = parser.value(scenario_option);
    config.measurement_duration_seconds = duration_seconds;
    if (!app_controller.startConfiguredCall(std::move(config)))
      return EXIT_FAILURE;
  }
  if (parser.isSet(gui_smoke_state_option)) {
    const auto run_state_probe = [&] {
      if (gui_smoke_state == QStringLiteral("create"))
        app_controller.showCreateRoom();
      else if (gui_smoke_state == QStringLiteral("join"))
        app_controller.showJoinRoom();
      std::cout << "GUI_STATE page=" << gui_smoke_state.toStdString()
                << " qml_loaded=1" << std::endl;
      if (gui_smoke_state == QStringLiteral("home") ||
          gui_smoke_state == QStringLiteral("create") ||
          gui_smoke_state == QStringLiteral("join")) {
        auto *root = engine.rootObjects().isEmpty()
                         ? nullptr
                         : engine.rootObjects().constFirst();
        const std::array<QString, 8> object_names{
            QStringLiteral("createRoomButton"),
            QStringLiteral("joinRoomButton"),
            QStringLiteral("recentRoomAction"),
            QStringLiteral("roomCodeField"),
            QStringLiteral("microphoneIntentControl"),
            QStringLiteral("speakerIntentControl"),
            QStringLiteral("qualityProfileControl"),
            QStringLiteral("preflightPrimaryButton")};
        const auto expected_for_state = [&](const QString &object_name) {
          if (gui_smoke_state == QStringLiteral("home"))
            return object_name == QStringLiteral("createRoomButton") ||
                   object_name == QStringLiteral("joinRoomButton") ||
                   object_name == QStringLiteral("recentRoomAction");
          if (gui_smoke_state == QStringLiteral("create"))
            return object_name == QStringLiteral("qualityProfileControl") ||
                   object_name == QStringLiteral("microphoneIntentControl") ||
                   object_name == QStringLiteral("speakerIntentControl") ||
                   object_name == QStringLiteral("preflightPrimaryButton");
          return object_name == QStringLiteral("roomCodeField") ||
                 object_name == QStringLiteral("microphoneIntentControl") ||
                 object_name == QStringLiteral("speakerIntentControl") ||
                 object_name == QStringLiteral("qualityProfileControl") ||
                 object_name == QStringLiteral("preflightPrimaryButton");
        };
        for (const auto &object_name : object_names) {
          const auto present = root && expected_for_state(object_name) &&
                               root->findChild<QObject *>(object_name);
          std::cout << "GUI_OBJECT " << object_name.toStdString() << "="
                    << (present ? 1 : 0) << std::endl;
        }
      } else if (gui_smoke_state == QStringLiteral("call-host") ||
                 gui_smoke_state == QStringLiteral("call-viewer")) {
        auto *root = engine.rootObjects().isEmpty()
                         ? nullptr
                         : engine.rootObjects().constFirst();
        const std::array<QString, 6> object_names{
            QStringLiteral("callPage"),
            QStringLiteral("microphoneControl"),
            QStringLiteral("speakerControl"),
            QStringLiteral("detailsControl"),
            QStringLiteral("leaveControl"),
            QStringLiteral("shareControl")};
        for (const auto &object_name : object_names) {
          const auto present = root && root->findChild<QObject *>(object_name);
          std::cout << "GUI_OBJECT " << object_name.toStdString() << "="
                    << (present ? 1 : 0) << std::endl;
        }
      }
      QTimer::singleShot(80, &app, &QCoreApplication::quit);
    };
    if (gui_smoke_state == QStringLiteral("call-host-actions")) {
      QTimer::singleShot(120, &app, [&] {
        auto *root = engine.rootObjects().isEmpty()
                         ? nullptr
                         : engine.rootObjects().constFirst();
        auto invoke_click = [root](const char *name) {
          auto *control = root ? root->findChild<QObject *>(name) : nullptr;
          return control && QMetaObject::invokeMethod(
                                control, "clicked", Qt::DirectConnection);
        };
        const auto microphone_clicked = invoke_click("microphoneControl");
        const auto speaker_clicked = invoke_click("speakerControl");
        auto *call_page = root ? root->findChild<QObject *>("callPage") : nullptr;
        const auto details_clicked = invoke_click("detailsControl");
        const auto drawer_open =
            call_page && call_page->property("detailsOpen").toBool();
        const auto voice_panel =
            root && root->findChild<QObject *>("voicePanel") != nullptr;
        auto *volume_control =
            root ? root->findChild<QObject *>("speakerVolumeControl") : nullptr;
        QVariant volume_restored;
        const auto volume_checked =
            volume_control && QMetaObject::invokeMethod(
                                  volume_control, "requestVolume",
                                  Qt::DirectConnection,
                                  Q_RETURN_ARG(QVariant, volume_restored),
                                  Q_ARG(QVariant, 37));
        const auto leave_clicked = invoke_click("leaveControl");
        const auto returned_home = app_controller.page() == QStringLiteral("home");
        std::cout << "GUI_ACTION microphone=" << microphone_clicked
                  << " speaker=" << speaker_clicked
                  << " drawer=" << (details_clicked && drawer_open)
                  << " voice_panel=" << voice_panel
                  << " volume_rejected_restored="
                  << (volume_checked && volume_restored.toBool())
                  << " leave=" << (leave_clicked && returned_home)
                  << " page=" << app_controller.page().toStdString()
                  << std::endl;
        QTimer::singleShot(20, &app, &QCoreApplication::quit);
      });
    } else {
      QTimer::singleShot(0, &app, run_state_probe);
    }
  }
  return app.exec();
}
