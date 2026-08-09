#include "launch_options.hpp"

namespace shareme::tools {

LaunchClassification classify_launch(bool any_rtc_option, bool server_set,
                                      QString role) {
  if (!any_rtc_option)
    return {.accepted = true, .interactive = true};
  if (!server_set ||
      (role != QStringLiteral("host") && role != QStringLiteral("viewer")))
    return {.category = QStringLiteral("incomplete-cli")};
  return {.accepted = true};
}

} // namespace shareme::tools
