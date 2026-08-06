#include "shareme/core/playback_failure.hpp"

#include <array>

namespace shareme::core {
namespace {

struct PlaybackFailureRow {
  PlaybackCategory category;
  std::string_view name;
  PlaybackImpact impact;
};

constexpr std::array kPlaybackFailureRows{
    PlaybackFailureRow{PlaybackCategory::audio_output_would_block,
                       "audio-output-would-block", PlaybackImpact::transient},
    PlaybackFailureRow{PlaybackCategory::audio_output_failure,
                       "audio-output-failure", PlaybackImpact::recoverable},
    PlaybackFailureRow{PlaybackCategory::audio_queue_overflow,
                       "audio-queue-overflow", PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::audio_consumption_unknown,
                       "audio-consumption-unknown", PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::audio_correlation_unavailable,
                       "audio-correlation-unavailable", PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::audio_correlation_residual_exceeded,
                       "audio-correlation-residual-exceeded",
                       PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::audio_format_change,
                       "audio-format-change", PlaybackImpact::recoverable},
    PlaybackFailureRow{PlaybackCategory::audio_output_device_lost,
                       "audio-output-device-lost", PlaybackImpact::recoverable},
    PlaybackFailureRow{PlaybackCategory::route_activation_failed,
                       "route-activation-failed", PlaybackImpact::recoverable},
    PlaybackFailureRow{PlaybackCategory::route_candidate_stale,
                       "route-candidate-stale", PlaybackImpact::recoverable},
    PlaybackFailureRow{PlaybackCategory::route_handoff_unknown_consumption,
                       "route-handoff-unknown-consumption",
                       PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::route_no_active_output,
                       "route-no-active-output", PlaybackImpact::fatal_to_renderer},
    PlaybackFailureRow{PlaybackCategory::early_hold_limit, "early-hold-limit",
                       PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::hard_resync_timeout,
                       "hard-resync-timeout", PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::hard_resync_clock_lost,
                       "hard-resync-clock-lost", PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::hard_resync_route_transition,
                       "hard-resync-route-transition", PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::hard_resync_frame_limit,
                       "hard-resync-frame-limit", PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::hard_resync_attempt_limit,
                       "hard-resync-attempt-limit", PlaybackImpact::degraded},
    PlaybackFailureRow{PlaybackCategory::hard_resync_end_of_stream,
                       "hard-resync-end-of-stream", PlaybackImpact::recoverable},
    PlaybackFailureRow{PlaybackCategory::movie_audio_transport_failure,
                       "movie-audio-transport-failure", PlaybackImpact::fatal_to_call},
};

const PlaybackFailureRow* find_row(PlaybackCategory category) noexcept {
  for (const auto& row : kPlaybackFailureRows) {
    if (row.category == category) {
      return &row;
    }
  }
  return nullptr;
}

}  // namespace

std::string_view playback_category_name(PlaybackCategory category) noexcept {
  const auto* row = find_row(category);
  return row == nullptr ? std::string_view{"unknown-playback-failure"}
                        : row->name;
}

PlaybackImpact playback_category_impact(PlaybackCategory category) noexcept {
  const auto* row = find_row(category);
  return row == nullptr ? PlaybackImpact::degraded : row->impact;
}

}  // namespace shareme::core
