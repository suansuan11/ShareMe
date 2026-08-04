#include "playout_report.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void require(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "Requirement failed at line " << line << ": " << expression
              << '\n';
    std::exit(EXIT_FAILURE);
  }
}
} // namespace

#define REQUIRE(expression) require((expression), #expression, __LINE__)

int main() {
  using shareme::tools::PlaybackState;
  using shareme::tools::PlayoutReport;
  using shareme::tools::PlayoutReportTracker;
  using shareme::tools::RenderedPlayoutTracker;
  using shareme::tools::decode_playout_report;
  using shareme::tools::encode_playout_report;
  using shareme::tools::reconcile_rendered_frame;
  using shareme::tools::viewer_delta_ms;

  const PlaybackState anchor{.room_id = QStringLiteral("ABC234"),
                             .sequence = 5,
                             .state = QStringLiteral("playing"),
                             .media_pts_ms = 10'000,
                             .effective_at_host_time_ms = 500,
                             .rate = 1.0,
                             .generation = 3,
                             .video_anchor_media_pts_ms = 10'000,
                             .video_rtp_timestamp = 0xfffffff0U};
  const auto wrapped = reconcile_rendered_frame(anchor, 0x0000004aU, 2);
  REQUIRE(wrapped.has_value());
  REQUIRE(wrapped->rendered_pts_ms == 10'001);
  REQUIRE(wrapped->buffer_ms == 1);

  auto reverse_anchor = anchor;
  reverse_anchor.video_rtp_timestamp = 20;
  const auto reverse =
      reconcile_rendered_frame(reverse_anchor, 0xffffffbaU, 0);
  REQUIRE(reverse.has_value());
  REQUIRE(reverse->rendered_pts_ms == 9'999);

  auto rounding_anchor = anchor;
  rounding_anchor.video_rtp_timestamp = 1'000;
  const auto rounded_forward =
      reconcile_rendered_frame(rounding_anchor, 1'135, 2);
  const auto rounded_backward =
      reconcile_rendered_frame(rounding_anchor, 865, 0);
  REQUIRE(rounded_forward.has_value());
  REQUIRE(rounded_forward->rendered_pts_ms == 10'002);
  REQUIRE(rounded_backward.has_value());
  REQUIRE(rounded_backward->rendered_pts_ms == 9'998);

  RenderedPlayoutTracker rendered_tracker;
  REQUIRE(!rendered_tracker.accept(anchor, *reverse));
  REQUIRE(rendered_tracker.accept(anchor, *wrapped));
  REQUIRE(!rendered_tracker.accept(anchor, *reverse));
  auto next_anchor = anchor;
  next_anchor.generation = 4;
  next_anchor.video_anchor_media_pts_ms = 20'000;
  auto old_generation_sample = *wrapped;
  REQUIRE(!rendered_tracker.accept(next_anchor, old_generation_sample));
  rendered_tracker.reset();
  auto next_sample = old_generation_sample;
  next_sample.generation = 4;
  next_sample.rendered_pts_ms = 20'001;
  REQUIRE(rendered_tracker.accept(next_anchor, next_sample));

  REQUIRE(!reconcile_rendered_frame(anchor,
                                    *anchor.video_rtp_timestamp + 900'001U, 0));
  REQUIRE(viewer_delta_ms(1'000, 900) == 100);
  REQUIRE(viewer_delta_ms(900, 1'000) == -100);
  REQUIRE(!viewer_delta_ms(std::numeric_limits<std::int64_t>::max(), -1));
  REQUIRE(!viewer_delta_ms(std::numeric_limits<std::int64_t>::min(), 1));

  const PlayoutReport report{.room_id = QStringLiteral("ABC234"),
                             .sequence = 9,
                             .rendered_pts_ms = 12'345,
                             .buffer_ms = 160,
                             .receive_time_ms = 99'000,
                             .generation = 3};
  const auto encoded = encode_playout_report(report);
  const auto decoded =
      decode_playout_report(encoded, QStringLiteral("ABC234"));
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->rendered_pts_ms == report.rendered_pts_ms);
  REQUIRE(decoded->buffer_ms == report.buffer_ms);
  REQUIRE(decoded->generation == report.generation);

  auto invalid = report;
  invalid.buffer_ms = 10'001;
  REQUIRE(encode_playout_report(invalid).isEmpty());
  invalid = report;
  invalid.sequence = 0;
  REQUIRE(encode_playout_report(invalid).isEmpty());
  REQUIRE(!decode_playout_report(encoded, QStringLiteral("OTHER2")));

  PlayoutReportTracker tracker;
  REQUIRE(tracker.accept(report, 3));
  REQUIRE(!tracker.accept(report, 3));
  auto stale_generation = report;
  stale_generation.sequence = 10;
  stale_generation.generation = 2;
  REQUIRE(!tracker.accept(stale_generation, 3));
  auto next = report;
  next.sequence = 11;
  next.generation = 4;
  REQUIRE(tracker.accept(next, 4));
  REQUIRE(tracker.last().has_value());
  REQUIRE(tracker.last()->generation == 4);
}
