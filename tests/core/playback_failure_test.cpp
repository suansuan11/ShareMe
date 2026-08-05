#include "shareme/core/playback_failure.hpp"

#include <array>
#include <concepts>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>

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

void exposes_the_complete_stable_failure_taxonomy() {
  using shareme::core::PlaybackCategory;
  using shareme::core::PlaybackImpact;
  using shareme::core::playback_category_impact;
  using shareme::core::playback_category_name;

  static_assert(std::is_enum_v<PlaybackCategory>);
  static_assert(std::is_enum_v<PlaybackImpact>);

  struct Expected {
    PlaybackCategory category;
    std::string_view name;
    PlaybackImpact impact;
  };

  constexpr std::array expected{
      Expected{PlaybackCategory::audio_output_would_block,
               "audio-output-would-block", PlaybackImpact::transient},
      Expected{PlaybackCategory::audio_output_failure, "audio-output-failure",
               PlaybackImpact::recoverable},
      Expected{PlaybackCategory::audio_queue_overflow, "audio-queue-overflow",
               PlaybackImpact::degraded},
      Expected{PlaybackCategory::audio_consumption_unknown,
               "audio-consumption-unknown", PlaybackImpact::degraded},
      Expected{PlaybackCategory::audio_correlation_unavailable,
               "audio-correlation-unavailable", PlaybackImpact::degraded},
      Expected{PlaybackCategory::audio_correlation_residual_exceeded,
               "audio-correlation-residual-exceeded", PlaybackImpact::degraded},
      Expected{PlaybackCategory::audio_format_change, "audio-format-change",
               PlaybackImpact::recoverable},
      Expected{PlaybackCategory::audio_output_device_lost,
               "audio-output-device-lost", PlaybackImpact::recoverable},
      Expected{PlaybackCategory::route_activation_failed,
               "route-activation-failed", PlaybackImpact::recoverable},
      Expected{PlaybackCategory::route_candidate_stale,
               "route-candidate-stale", PlaybackImpact::recoverable},
      Expected{PlaybackCategory::route_handoff_unknown_consumption,
               "route-handoff-unknown-consumption", PlaybackImpact::degraded},
      Expected{PlaybackCategory::route_no_active_output,
               "route-no-active-output", PlaybackImpact::fatal_to_renderer},
      Expected{PlaybackCategory::early_hold_limit, "early-hold-limit",
               PlaybackImpact::degraded},
      Expected{PlaybackCategory::hard_resync_timeout, "hard-resync-timeout",
               PlaybackImpact::degraded},
      Expected{PlaybackCategory::hard_resync_clock_lost,
               "hard-resync-clock-lost", PlaybackImpact::degraded},
      Expected{PlaybackCategory::hard_resync_route_transition,
               "hard-resync-route-transition", PlaybackImpact::degraded},
      Expected{PlaybackCategory::hard_resync_frame_limit,
               "hard-resync-frame-limit", PlaybackImpact::degraded},
      Expected{PlaybackCategory::hard_resync_attempt_limit,
               "hard-resync-attempt-limit", PlaybackImpact::degraded},
      Expected{PlaybackCategory::hard_resync_end_of_stream,
               "hard-resync-end-of-stream", PlaybackImpact::recoverable},
      Expected{PlaybackCategory::movie_audio_transport_failure,
               "movie-audio-transport-failure", PlaybackImpact::fatal_to_call},
  };

  static_assert(expected.size() == 20);
  static_assert(std::same_as<
                decltype(playback_category_name(PlaybackCategory::early_hold_limit)),
                std::string_view>);
  static_assert(std::same_as<
                decltype(playback_category_impact(PlaybackCategory::early_hold_limit)),
                PlaybackImpact>);

  for (const auto& item : expected) {
    REQUIRE(playback_category_name(item.category) == item.name);
    REQUIRE(playback_category_impact(item.category) == item.impact);
  }
}

}  // namespace

int main() {
  exposes_the_complete_stable_failure_taxonomy();
  return EXIT_SUCCESS;
}
