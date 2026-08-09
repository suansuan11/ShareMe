#pragma once

#include <QString>

namespace shareme::tools {

struct LaunchClassification {
  bool accepted{false};
  bool interactive{false};
  QString category;
};

[[nodiscard]] LaunchClassification classify_launch(bool any_rtc_option,
                                                    bool server_set,
                                                    QString role);

} // namespace shareme::tools
