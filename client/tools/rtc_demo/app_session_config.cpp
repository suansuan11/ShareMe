#include "app_session_config.hpp"

namespace shareme::tools {
namespace {

[[nodiscard]] bool is_room_character(const QChar value) {
  return (value >= QLatin1Char('A') && value <= QLatin1Char('Z')) ||
         (value >= QLatin1Char('2') && value <= QLatin1Char('7'));
}

} // namespace

QString normalize_room_code(QString room) {
  room = room.trimmed().toUpper();
  QString normalized;
  normalized.reserve(6);
  for (const auto value : room) {
    if (value == QLatin1Char('-') || value.isSpace())
      continue;
    if (!is_room_character(value))
      return {};
    normalized.append(value);
  }
  return normalized.size() == 6 ? normalized : QString{};
}

QString format_room_code(QString room) {
  const auto normalized = normalize_room_code(std::move(room));
  if (normalized.isEmpty())
    return {};
  return normalized.left(3) + QLatin1Char('-') + normalized.mid(3);
}

QString screen_profile_name(shareme::core::ScreenStreamProfile profile) {
  switch (profile) {
  case shareme::core::ScreenStreamProfile::standard:
    return QStringLiteral("standard");
  case shareme::core::ScreenStreamProfile::quality:
    return QStringLiteral("quality");
  case shareme::core::ScreenStreamProfile::cinema:
    return QStringLiteral("cinema");
  }
  return {};
}

AppConfigValidation
validate_interactive_config(const AppSessionConfig &config) {
  if (!config.server_url.isValid() || config.server_url.host().isEmpty() ||
      (config.server_url.scheme() != QStringLiteral("ws") &&
       config.server_url.scheme() != QStringLiteral("wss"))) {
    return {.category = QStringLiteral("invalid-server"),
            .message = QStringLiteral("请输入有效的 WebSocket 服务地址")};
  }
  if (config.role == InteractiveRole::viewer &&
      normalize_room_code(config.requested_room).isEmpty()) {
    return {.category = QStringLiteral("invalid-room"),
            .message = QStringLiteral("请输入有效的六位房间码")};
  }
  if (config.video_source != SessionVideoSource::screen) {
    return {.category = QStringLiteral("unsupported-source"),
            .message = QStringLiteral("图形界面当前仅支持屏幕共享通话")};
  }
  if (config.screen_encoder == ScreenEncoderMode::software &&
      config.screen_profile != shareme::core::ScreenStreamProfile::standard) {
    return {.category = QStringLiteral("software-standard-only"),
            .message = QStringLiteral(
                "software screen encoding requires standard")};
  }
  return {.accepted = true};
}

} // namespace shareme::tools
