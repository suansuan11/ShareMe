#include "app_session_config.hpp"

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

void normalizes_human_room_input_without_inventing_characters() {
  REQUIRE(shareme::tools::normalize_room_code(QStringLiteral("  kdglmd  ")) ==
          QStringLiteral("KDGLMD"));
  REQUIRE(shareme::tools::normalize_room_code(QStringLiteral("kdg-lmd")) ==
          QStringLiteral("KDGLMD"));
  REQUIRE(shareme::tools::format_room_code(QStringLiteral("KDGLMD")) ==
          QStringLiteral("KDG-LMD"));
  REQUIRE(shareme::tools::normalize_room_code(QStringLiteral("a@c")) ==
          QString());
}

void validates_interactive_host_and_viewer_boundaries() {
  using shareme::tools::AppSessionConfig;
  using shareme::tools::InteractiveRole;

  AppSessionConfig host;
  host.server_url = QUrl(QStringLiteral("ws://127.0.0.1:8080/v1/ws"));
  host.role = InteractiveRole::host;
  host.video_source = shareme::tools::SessionVideoSource::screen;
  REQUIRE(shareme::tools::validate_interactive_config(host).accepted);
  REQUIRE(host.screen_encoder ==
          shareme::tools::ScreenEncoderMode::auto_mode);

  host.screen_encoder = shareme::tools::ScreenEncoderMode::software;
  host.screen_profile = shareme::core::ScreenStreamProfile::quality;
  const auto software_quality =
      shareme::tools::validate_interactive_config(host);
  REQUIRE(!software_quality.accepted);
  REQUIRE(software_quality.category ==
          QStringLiteral("software-standard-only"));
  host.screen_profile = shareme::core::ScreenStreamProfile::standard;
  REQUIRE(shareme::tools::validate_interactive_config(host).accepted);

  AppSessionConfig viewer = host;
  viewer.role = InteractiveRole::viewer;
  viewer.video_source = shareme::tools::SessionVideoSource::screen;
  viewer.requested_room = QStringLiteral("ABC234");
  REQUIRE(shareme::tools::validate_interactive_config(viewer).accepted);

  viewer.requested_room.clear();
  const auto missing_room =
      shareme::tools::validate_interactive_config(viewer);
  REQUIRE(!missing_room.accepted);
  REQUIRE(missing_room.category == QStringLiteral("invalid-room"));

  host.server_url = QUrl(QStringLiteral("https://example.invalid/v1/ws"));
  const auto invalid_server = shareme::tools::validate_interactive_config(host);
  REQUIRE(!invalid_server.accepted);
  REQUIRE(invalid_server.category == QStringLiteral("invalid-server"));
}

void exposes_stable_profile_names() {
  using shareme::core::ScreenStreamProfile;
  REQUIRE(shareme::tools::screen_profile_name(ScreenStreamProfile::standard) ==
          QStringLiteral("standard"));
  REQUIRE(shareme::tools::screen_profile_name(ScreenStreamProfile::quality) ==
          QStringLiteral("quality"));
  REQUIRE(shareme::tools::screen_profile_name(ScreenStreamProfile::cinema) ==
          QStringLiteral("cinema"));
}

} // namespace

int main() {
  normalizes_human_room_input_without_inventing_characters();
  validates_interactive_host_and_viewer_boundaries();
  exposes_stable_profile_names();
  return EXIT_SUCCESS;
}
