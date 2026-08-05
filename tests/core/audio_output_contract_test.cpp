#include "shareme/core/audio_output_contract.hpp"
#include "shareme/core/playback_failure.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
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

void describes_pcm_in_audio_frame_units() {
  using shareme::core::AudioPcmBlockView;

  const AudioPcmBlockView pcm{
      .receiver_sequence = 17,
      .frame_count = 480,
      .sample_rate = 48'000,
      .channel_count = 2,
      .sample_format = {},
      .interleaving = {},
  };

  static_assert(std::is_enum_v<decltype(AudioPcmBlockView::sample_format)>);
  static_assert(std::is_enum_v<decltype(AudioPcmBlockView::interleaving)>);

  REQUIRE(pcm.receiver_sequence == 17);
  REQUIRE(pcm.frame_count == 480);
  REQUIRE(pcm.sample_rate == 48'000);
  REQUIRE(pcm.channel_count == 2);

  const auto scalar_samples = pcm.frame_count * pcm.channel_count;
  REQUIRE(scalar_samples == 960);

  const double duration_ms =
      1'000.0 * static_cast<double>(pcm.frame_count) / pcm.sample_rate;
  REQUIRE(std::abs(duration_ms - 10.0) < 1e-12);
}

void separates_per_call_write_results_from_device_totals() {
  using shareme::core::AudioDeviceSnapshot;
  using shareme::core::PlaybackCategory;
  using shareme::core::WriteResult;
  using shareme::core::WriteStatus;

  const WriteResult accepted{
      .status = WriteStatus::accepted,
      .accepted_frames = 480,
      .failure_category = std::nullopt,
  };
  const WriteResult would_block{
      .status = WriteStatus::would_block,
      .accepted_frames = 0,
      .failure_category = std::nullopt,
  };
  const WriteResult failed{
      .status = WriteStatus::failed,
      .accepted_frames = 0,
      .failure_category = PlaybackCategory::audio_output_failure,
  };

  REQUIRE(accepted.status == WriteStatus::accepted);
  REQUIRE(accepted.accepted_frames > 0);
  REQUIRE(would_block.status == WriteStatus::would_block);
  REQUIRE(would_block.accepted_frames == 0);
  REQUIRE(!would_block.failure_category.has_value());
  REQUIRE(failed.status == WriteStatus::failed);
  REQUIRE(failed.accepted_frames == 0);
  REQUIRE(failed.failure_category.has_value());
  REQUIRE(*failed.failure_category == PlaybackCategory::audio_output_failure);

  const AudioDeviceSnapshot snapshot{
      .device_instance_id = 9,
      .snapshot_sequence = 4,
      .accepted_frames_total = 960,
      .device_consumed_frames_total = 480,
      .device_queue_frames = 480,
      .output_latency_frames = 96,
      .underrun_count = 1,
      .discontinuity_count = 2,
      .active = true,
  };

  REQUIRE(snapshot.device_instance_id == 9);
  REQUIRE(snapshot.snapshot_sequence == 4);
  REQUIRE(snapshot.accepted_frames_total == 960);
  REQUIRE(snapshot.device_consumed_frames_total == 480);
  REQUIRE(snapshot.device_queue_frames == 480);
  REQUIRE(snapshot.output_latency_frames.has_value());
  REQUIRE(*snapshot.output_latency_frames == 96);
  REQUIRE(snapshot.underrun_count == 1);
  REQUIRE(snapshot.discontinuity_count == 2);
  REQUIRE(snapshot.active);
  REQUIRE(accepted.accepted_frames != snapshot.accepted_frames_total);
}

void final_snapshots_are_quiesced_and_report_consumption_exactness() {
  using shareme::core::AudioDeviceSnapshot;
  using shareme::core::FinalDeviceSnapshot;

  const AudioDeviceSnapshot ordinary{
      .device_instance_id = 12,
      .snapshot_sequence = 8,
      .accepted_frames_total = 1'440,
      .device_consumed_frames_total = 960,
      .device_queue_frames = 480,
      .output_latency_frames = std::nullopt,
      .underrun_count = 0,
      .discontinuity_count = 0,
      .active = true,
  };

  FinalDeviceSnapshot exact{};
  exact.device_instance_id = ordinary.device_instance_id;
  exact.snapshot_sequence = ordinary.snapshot_sequence;
  exact.accepted_frames_total = ordinary.accepted_frames_total;
  exact.device_consumed_frames_total = ordinary.device_consumed_frames_total;
  exact.device_queue_frames = ordinary.device_queue_frames;
  exact.output_latency_frames = ordinary.output_latency_frames;
  exact.underrun_count = ordinary.underrun_count;
  exact.discontinuity_count = ordinary.discontinuity_count;
  exact.active = ordinary.active;
  exact.quiesced = true;
  exact.exact_consumption = true;

  const FinalDeviceSnapshot frozen = exact;
  REQUIRE(frozen.device_instance_id == 12);
  REQUIRE(frozen.snapshot_sequence == 8);
  REQUIRE(frozen.quiesced);
  REQUIRE(frozen.exact_consumption);

  FinalDeviceSnapshot unknown = frozen;
  unknown.exact_consumption = false;
  REQUIRE(unknown.quiesced);
  REQUIRE(!unknown.exact_consumption);
}

}  // namespace

int main() {
  describes_pcm_in_audio_frame_units();
  separates_per_call_write_results_from_device_totals();
  final_snapshots_are_quiesced_and_report_consumption_exactness();
  return EXIT_SUCCESS;
}
