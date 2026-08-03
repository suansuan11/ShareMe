#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

namespace shareme::tools {

struct PlaybackState {
  QString room_id;
  std::uint64_t sequence{};
  QString state;
  std::int64_t media_pts_ms{};
  std::int64_t effective_at_host_time_ms{};
  double rate{};
  std::uint64_t generation{};
};

[[nodiscard]] QByteArray encode_playback_state(const PlaybackState &state);
[[nodiscard]] std::optional<PlaybackState>
decode_playback_state(const QByteArray &message, const QString &expected_room);
[[nodiscard]] std::optional<PlaybackState> make_movie_playback_state(
    QString room_id, std::uint64_t sequence,
    std::optional<std::int64_t> last_media_pts_ms,
    std::int64_t effective_at_host_time_ms, bool ended);

class PlaybackStateTracker final {
public:
  [[nodiscard]] bool accept(const PlaybackState &state) noexcept;
  [[nodiscard]] const std::optional<PlaybackState> &last() const noexcept;

private:
  std::optional<PlaybackState> last_;
};

} // namespace shareme::tools
