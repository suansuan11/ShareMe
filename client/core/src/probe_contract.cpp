#include "shareme/rtc/probe_contract.hpp"

#include <iomanip>
#include <locale>
#include <sstream>

namespace shareme::rtc {
namespace {

constexpr auto kMinimumDuration = std::chrono::milliseconds{1'000};
constexpr auto kMaximumDuration = std::chrono::milliseconds{30'000};
constexpr std::size_t kMaximumDiagnosticBytes = 256;

[[nodiscard]] std::string escape_json(std::string_view value) {
  constexpr char hex_digits[] = "0123456789abcdef";
  std::string escaped;
  escaped.reserve(value.size());
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (character < 0x20U) {
          escaped += "\\u00";
          escaped += hex_digits[(character >> 4U) & 0x0FU];
          escaped += hex_digits[character & 0x0FU];
        } else {
          escaped += static_cast<char>(character);
        }
        break;
    }
  }
  return escaped;
}

void append_optional_number(
    std::ostringstream& output,
    const std::optional<double>& value) {
  if (value.has_value()) {
    output << std::setprecision(15) << *value;
  } else {
    output << "null";
  }
}

}  // namespace

std::optional<std::string> validate(const ProbeConfig& config) {
  if (config.width <= 0 || config.height <= 0 || config.width % 2 != 0 ||
      config.height % 2 != 0) {
    return "video dimensions must be positive even integers";
  }
  if (config.frames_per_second < 1 || config.frames_per_second > 60) {
    return "frame rate must be between 1 and 60";
  }
  if (config.run_for < kMinimumDuration ||
      config.run_for > kMaximumDuration) {
    return "run duration must be between 1 and 30 seconds";
  }
  if (config.connect_timeout < kMinimumDuration ||
      config.connect_timeout > kMaximumDuration) {
    return "connection timeout must be between 1 and 30 seconds";
  }
  return std::nullopt;
}

std::string_view to_string(ProbeAudioMode mode) noexcept {
  switch (mode) {
    case ProbeAudioMode::synthetic:
      return "synthetic";
    case ProbeAudioMode::microphone:
      return "microphone";
  }
  return "unknown";
}

std::string_view to_string(ProbeStatus status) noexcept {
  switch (status) {
    case ProbeStatus::passed:
      return "passed";
    case ProbeStatus::timed_out:
      return "timed-out";
    case ProbeStatus::dependency_error:
      return "dependency-error";
    case ProbeStatus::permission_denied:
      return "permission-denied";
    case ProbeStatus::negotiation_failed:
      return "negotiation-failed";
    case ProbeStatus::media_failed:
      return "media-failed";
    case ProbeStatus::shutdown_failed:
      return "shutdown-failed";
  }
  return "unknown";
}

std::string to_json(
    const ProbeResult& result,
    std::string_view revision,
    std::string_view platform,
    std::string_view architecture) {
  const auto diagnostic =
      std::string_view{result.diagnostic}.substr(0, kMaximumDiagnosticBytes);

  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "{\"revision\":\"" << escape_json(revision) << "\","
         << "\"platform\":\"" << escape_json(platform) << "\","
         << "\"architecture\":\"" << escape_json(architecture) << "\","
         << "\"status\":\"" << to_string(result.status) << "\","
         << "\"connectionTimeMs\":" << result.connection_time.count() << ','
         << "\"videoFramesSent\":" << result.video_frames_sent << ','
         << "\"videoFramesReceived\":" << result.video_frames_received << ','
         << "\"videoFramesDropped\":" << result.video_frames_dropped << ','
         << "\"audioPacketsSent\":" << result.audio_packets_sent << ','
         << "\"audioPacketsReceived\":" << result.audio_packets_received << ','
         << "\"audioBytesSent\":" << result.audio_bytes_sent << ','
         << "\"audioBytesReceived\":" << result.audio_bytes_received << ','
         << "\"audioLevel\":";
  append_optional_number(output, result.audio_level);
  output << ",\"roundTripTimeMs\":";
  append_optional_number(output, result.round_trip_time_ms);
  output << ",\"candidateType\":\""
         << escape_json(result.selected_candidate_type) << "\","
         << "\"diagnostic\":\"" << escape_json(diagnostic) << "\"}";
  return output.str();
}

}  // namespace shareme::rtc
