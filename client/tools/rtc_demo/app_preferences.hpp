#pragma once

#include <QString>

class QSettings;

namespace shareme::tools {

class AppPreferences final {
public:
  explicit AppPreferences(QSettings &settings);

  [[nodiscard]] QString recent_room() const;
  void remember_room(QString room);
  void forget_recent_room();

private:
  QSettings &settings_;
};

} // namespace shareme::tools
