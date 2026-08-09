#pragma once

#include "shareme/core/screen_stream_profile.hpp"
#include "shareme/rtc/signaled_peer.hpp"

#include <QUrl>
#include <QString>

#include <cstdint>
#include <filesystem>

namespace shareme::tools {

enum class AppPage { home, preflight, calling, result };
enum class PreflightMode { create_room, join_room };
enum class InteractiveRole { host, viewer };
enum class SessionVideoSource { test, desktop, movie, screen };

struct AppSessionConfig {
  QUrl server_url{QStringLiteral("ws://127.0.0.1:8080/v1/ws")};
  InteractiveRole role{InteractiveRole::host};
  QString requested_room;
  SessionVideoSource video_source{SessionVideoSource::screen};
  shareme::core::ScreenStreamProfile screen_profile{
      shareme::core::ScreenStreamProfile::standard};
  shareme::rtc::SignaledAudioMode audio_mode{
      shareme::rtc::SignaledAudioMode::microphone};
  bool native_audio_playout{true};
  std::filesystem::path movie_path;
  bool movie_audio{false};
  QString video_acceleration{QStringLiteral("software")};
  QString metrics_jsonl_path;
  QString drift_scenario_name;
  std::int64_t measurement_duration_seconds{0};
};

struct AppConfigValidation {
  bool accepted{false};
  QString category;
  QString message;
};

[[nodiscard]] QString normalize_room_code(QString room);
[[nodiscard]] QString format_room_code(QString room);
[[nodiscard]] QString
screen_profile_name(shareme::core::ScreenStreamProfile profile);
[[nodiscard]] AppConfigValidation
validate_interactive_config(const AppSessionConfig &config);

} // namespace shareme::tools
