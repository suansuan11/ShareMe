#include "app_preferences.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}
#define REQUIRE(expression) require((expression), #expression, __LINE__)
} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  QTemporaryDir directory;
  REQUIRE(directory.isValid());
  QSettings settings(directory.filePath(QStringLiteral("shareme.ini")),
                     QSettings::IniFormat);
  shareme::tools::AppPreferences preferences(settings);

  REQUIRE(preferences.recent_room().isEmpty());
  preferences.remember_room(QStringLiteral("abc234"));
  REQUIRE(preferences.recent_room() == QStringLiteral("ABC234"));
  preferences.remember_room(QStringLiteral("DEF567"));
  REQUIRE(preferences.recent_room() == QStringLiteral("DEF567"));
  REQUIRE(settings.allKeys().size() == 1);
  REQUIRE(!settings.contains(QStringLiteral("serverUrl")));
  REQUIRE(!settings.contains(QStringLiteral("moviePath")));
  preferences.forget_recent_room();
  REQUIRE(preferences.recent_room().isEmpty());
  return EXIT_SUCCESS;
}
