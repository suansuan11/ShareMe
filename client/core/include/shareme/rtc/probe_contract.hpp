#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace shareme::rtc {

enum class ProbeAudioMode {
  synthetic,
  microphone,
};

enum class ProbeStatus {
  passed,
  timed_out,
  dependency_error,
  permission_denied,
  negotiation_failed,
  media_failed,
  shutdown_failed,
};

struct ProbeConfig {
  ProbeAudioMode audio_mode{ProbeAudioMode::synthetic};
  int width{640};
  int height{360};
  int frames_per_second{30};
  std::chrono::milliseconds run_for{3'000};
  std::chrono::milliseconds connect_timeout{10'000};
};

struct ProbeResult {
  ProbeStatus status{ProbeStatus::dependency_error};
  std::chrono::milliseconds connection_time{0};
  std::uint64_t video_frames_sent{0};
  std::uint64_t video_frames_received{0};
  std::uint64_t video_frames_dropped{0};
  std::uint64_t audio_packets_sent{0};
  std::uint64_t audio_packets_received{0};
  std::uint64_t audio_bytes_sent{0};
  std::uint64_t audio_bytes_received{0};
  std::optional<double> audio_level;
  std::optional<double> round_trip_time_ms;
  std::string selected_candidate_type;
  std::string diagnostic;
};

[[nodiscard]] std::optional<std::string> validate(const ProbeConfig& config);
[[nodiscard]] std::string_view to_string(ProbeAudioMode mode) noexcept;
[[nodiscard]] std::string_view to_string(ProbeStatus status) noexcept;
[[nodiscard]] std::string to_json(
    const ProbeResult& result,
    std::string_view revision,
    std::string_view platform,
    std::string_view architecture);

}  // namespace shareme::rtc
