#pragma once

#include <string_view>

namespace shareme::core {

enum class PlaybackImpact {
  transient,
  recoverable,
  degraded,
  fatal_to_renderer,
  fatal_to_call,
};

enum class PlaybackCategory {
  audio_output_would_block,
  audio_output_failure,
  audio_queue_overflow,
  audio_consumption_unknown,
  audio_correlation_unavailable,
  audio_correlation_residual_exceeded,
  audio_format_change,
  audio_output_device_lost,
  route_activation_failed,
  route_candidate_stale,
  route_handoff_unknown_consumption,
  route_no_active_output,
  early_hold_limit,
  hard_resync_timeout,
  hard_resync_clock_lost,
  hard_resync_route_transition,
  hard_resync_frame_limit,
  hard_resync_attempt_limit,
  hard_resync_end_of_stream,
  movie_audio_transport_failure,
};

[[nodiscard]] std::string_view playback_category_name(
    PlaybackCategory category) noexcept;

[[nodiscard]] PlaybackImpact playback_category_impact(
    PlaybackCategory category) noexcept;

}  // namespace shareme::core
