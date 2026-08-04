#pragma once

#include "playback_state.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

namespace shareme::tools {

struct PlayoutReport {
  QString room_id;
  std::uint64_t sequence{};
  std::int64_t rendered_pts_ms{};
  std::int64_t buffer_ms{};
  std::int64_t receive_time_ms{};
  std::uint64_t generation{};
};

struct RenderedPlayoutSample {
  std::int64_t rendered_pts_ms{};
  std::int64_t buffer_ms{};
  std::uint64_t generation{};
};

[[nodiscard]] QByteArray encode_playout_report(const PlayoutReport &report);
[[nodiscard]] std::optional<PlayoutReport>
decode_playout_report(const QByteArray &message, const QString &expected_room);
[[nodiscard]] std::optional<RenderedPlayoutSample>
reconcile_rendered_frame(const PlaybackState &anchor,
                         std::uint32_t rendered_rtp_timestamp,
                         std::int64_t elapsed_since_anchor_ms) noexcept;
[[nodiscard]] std::optional<std::int64_t>
viewer_delta_ms(std::int64_t host_pts_ms,
                std::int64_t rendered_pts_ms) noexcept;

class PlayoutReportTracker final {
public:
  [[nodiscard]] bool accept(const PlayoutReport &report,
                            std::uint64_t expected_generation) noexcept;
  [[nodiscard]] const std::optional<PlayoutReport> &last() const noexcept;

private:
  std::optional<PlayoutReport> last_;
};

class RenderedPlayoutTracker final {
public:
  [[nodiscard]] bool accept(const PlaybackState &anchor,
                            const RenderedPlayoutSample &sample) noexcept;
  void reset() noexcept;
  [[nodiscard]] const std::optional<RenderedPlayoutSample> &last() const noexcept;

private:
  std::optional<RenderedPlayoutSample> last_;
};

} // namespace shareme::tools
