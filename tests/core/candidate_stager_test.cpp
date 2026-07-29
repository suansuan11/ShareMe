#include "shareme/rtc/candidate_stager.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition) {
    return;
  }

  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

template <std::size_t Capacity>
concept CanCreateCandidateStager = requires {
  typename shareme::rtc::CandidateStager<int, Capacity>;
};

static_assert(CanCreateCandidateStager<1>);
static_assert(!CanCreateCandidateStager<0>);

void bounds_entries_and_preserves_fifo_order() {
  shareme::rtc::CandidateStager<std::string, 3> stager;

  REQUIRE(stager.stage("one"));
  REQUIRE(stager.stage("two"));
  REQUIRE(stager.stage("three"));
  REQUIRE(!stager.stage("four"));
  REQUIRE(stager.size() == 3);
  REQUIRE(stager.overflow_count() == 1);

  const auto drained = stager.drain();
  REQUIRE(drained == std::vector<std::string>({"one", "two", "three"}));
  REQUIRE(stager.empty());
  REQUIRE(stager.drain().empty());
  REQUIRE(stager.overflow_count() == 1);
}

void clears_owned_values_without_resetting_observability() {
  shareme::rtc::CandidateStager<std::string, 2> stager;
  REQUIRE(stager.stage("one"));
  REQUIRE(stager.stage("two"));
  REQUIRE(!stager.stage("three"));

  stager.clear();

  REQUIRE(stager.empty());
  REQUIRE(stager.overflow_count() == 1);
  REQUIRE(stager.stage("replacement"));
  REQUIRE(stager.drain() == std::vector<std::string>({"replacement"}));
}

void supports_move_only_candidates() {
  shareme::rtc::CandidateStager<std::unique_ptr<int>, 2> stager;
  REQUIRE(stager.stage(std::make_unique<int>(7)));
  REQUIRE(stager.stage(std::make_unique<int>(9)));

  auto drained = stager.drain();
  REQUIRE(drained.size() == 2);
  REQUIRE(*drained[0] == 7);
  REQUIRE(*drained[1] == 9);
}

}  // namespace

int main() {
  bounds_entries_and_preserves_fifo_order();
  clears_owned_values_without_resetting_observability();
  supports_move_only_candidates();
  return EXIT_SUCCESS;
}
