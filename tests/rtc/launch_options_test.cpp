#include "launch_options.hpp"

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

int main() {
  using shareme::tools::classify_launch;
  const auto interactive = classify_launch(false, false, {});
  REQUIRE(interactive.accepted);
  REQUIRE(interactive.interactive);

  const auto host = classify_launch(true, true, QStringLiteral("host"));
  REQUIRE(host.accepted);
  REQUIRE(!host.interactive);

  const auto partial = classify_launch(true, true, {});
  REQUIRE(!partial.accepted);
  REQUIRE(partial.category == QStringLiteral("incomplete-cli"));

  const auto invalid = classify_launch(true, true, QStringLiteral("bad"));
  REQUIRE(!invalid.accepted);
  return EXIT_SUCCESS;
}
