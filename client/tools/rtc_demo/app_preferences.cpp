#include "app_preferences.hpp"

#include "app_session_config.hpp"

#include <QSettings>

namespace shareme::tools {
namespace {
constexpr auto kRecentRoomKey = "ui/recentRoom";
}

AppPreferences::AppPreferences(QSettings &settings) : settings_(settings) {}

QString AppPreferences::recent_room() const {
  return normalize_room_code(settings_.value(kRecentRoomKey).toString());
}

void AppPreferences::remember_room(QString room) {
  const auto normalized = normalize_room_code(std::move(room));
  if (!normalized.isEmpty())
    settings_.setValue(kRecentRoomKey, normalized);
}

void AppPreferences::forget_recent_room() { settings_.remove(kRecentRoomKey); }

} // namespace shareme::tools
