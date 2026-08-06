#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

namespace shareme::tools {

struct MovieAudioClockMessage {
  QString room_id;
  std::uint64_t sequence{};
  std::uint64_t playback_generation{};
  std::uint64_t audio_epoch{};
  std::uint64_t host_source_sequence{};
  std::int64_t media_pts_ms{};
  std::uint32_t sample_rate{};
  std::uint16_t channel_count{};
};

[[nodiscard]] QByteArray
encode_movie_audio_clock(const MovieAudioClockMessage &message);
[[nodiscard]] std::optional<MovieAudioClockMessage>
decode_movie_audio_clock(const QByteArray &message, const QString &expected_room);

class MovieAudioClockTracker final {
public:
  [[nodiscard]] bool accept(const MovieAudioClockMessage &message) noexcept;
  [[nodiscard]] const std::optional<MovieAudioClockMessage> &
  last() const noexcept;

private:
  std::optional<MovieAudioClockMessage> last_;
};

} // namespace shareme::tools
