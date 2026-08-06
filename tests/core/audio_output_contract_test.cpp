#include "shareme/core/audio_output_contract.hpp"
#include "shareme/core/playback_failure.hpp"

#include <concepts>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace {

template <typename Device>
concept AudioOutputOperations = requires(
    Device& device, shareme::core::AudioPcmBlockView pcm) {
  { device.try_write(pcm) } -> std::same_as<shareme::core::WriteResult>;
  { device.snapshot() } -> std::same_as<shareme::core::AudioDeviceSnapshot>;
  { device.quiesce_and_snapshot() } ->
      std::same_as<shareme::core::FinalDeviceSnapshot>;
};

template <typename Device>
concept AudioLifecycleOperations = requires(Device& device) {
  device.start();
  device.pause();
  device.stop();
};

void asserts_portable_value_types_and_audio_frame_units() {
  using shareme::core::AudioPcmBlockView;
  using shareme::core::AudioDeviceSnapshot;
  using shareme::core::FinalDeviceSnapshot;
  using shareme::core::PlaybackCategory;
  using shareme::core::WriteResult;
  using shareme::core::WriteStatus;

  static_assert(std::same_as<decltype(AudioPcmBlockView::receiver_sequence),
                             std::uint64_t>);
  static_assert(std::same_as<decltype(AudioPcmBlockView::frame_count),
                             std::uint64_t>);
  static_assert(std::same_as<decltype(AudioPcmBlockView::sample_rate),
                             std::uint32_t>);
  static_assert(std::same_as<decltype(AudioPcmBlockView::channel_count),
                             std::uint16_t>);
  static_assert(std::is_enum_v<decltype(AudioPcmBlockView::sample_format)>);
  static_assert(std::is_enum_v<decltype(AudioPcmBlockView::interleaving)>);
  static_assert(!std::same_as<decltype(AudioPcmBlockView::sample_format),
                              decltype(AudioPcmBlockView::interleaving)>);

  static_assert(std::is_enum_v<WriteStatus>);
  static_assert(std::same_as<decltype(WriteResult::status), WriteStatus>);
  static_assert(std::same_as<decltype(WriteResult::accepted_frames),
                             std::uint64_t>);
  static_assert(std::same_as<decltype(WriteResult::failure_category),
                             std::optional<PlaybackCategory>>);

  static_assert(std::same_as<decltype(AudioDeviceSnapshot::device_instance_id),
                             std::uint64_t>);
  static_assert(std::same_as<decltype(AudioDeviceSnapshot::snapshot_sequence),
                             std::uint64_t>);
  static_assert(std::same_as<
                decltype(AudioDeviceSnapshot::accepted_frames_total),
                std::uint64_t>);
  static_assert(std::same_as<
                decltype(AudioDeviceSnapshot::device_consumed_frames_total),
                std::uint64_t>);
  static_assert(std::same_as<decltype(AudioDeviceSnapshot::device_queue_frames),
                             std::uint64_t>);
  static_assert(std::same_as<
                decltype(AudioDeviceSnapshot::output_latency_frames),
                std::optional<std::uint64_t>>);
  static_assert(std::same_as<decltype(AudioDeviceSnapshot::underrun_count),
                             std::uint64_t>);
  static_assert(std::same_as<
                decltype(AudioDeviceSnapshot::discontinuity_count),
                std::uint64_t>);
  static_assert(std::same_as<decltype(AudioDeviceSnapshot::active), bool>);
  static_assert(std::same_as<decltype(FinalDeviceSnapshot::quiesced), bool>);
  static_assert(
      std::same_as<decltype(FinalDeviceSnapshot::exact_consumption), bool>);

  constexpr AudioPcmBlockView pcm{
      .receiver_sequence = 17,
      .frame_count = 480,
      .sample_rate = 48'000,
      .channel_count = 2,
      .sample_format = {},
      .interleaving = {},
  };
  static_assert(pcm.receiver_sequence == 17);
  static_assert(pcm.frame_count == 480);
  static_assert(pcm.sample_rate == 48'000);
  static_assert(pcm.channel_count == 2);
  static_assert(pcm.frame_count * pcm.channel_count == 960);
  static_assert(pcm.frame_count * 1'000 == 10 * pcm.sample_rate);
}

void asserts_write_and_snapshot_value_contracts_without_a_device_factory() {
  using shareme::core::AudioDeviceSnapshot;
  using shareme::core::FinalDeviceSnapshot;
  using shareme::core::PlaybackCategory;
  using shareme::core::WriteResult;
  using shareme::core::WriteStatus;

  constexpr WriteResult accepted{
      .status = WriteStatus::accepted,
      .accepted_frames = 480,
      .failure_category = std::nullopt,
  };
  constexpr WriteResult would_block{
      .status = WriteStatus::would_block,
      .accepted_frames = 0,
      .failure_category = std::nullopt,
  };
  constexpr WriteResult failed{
      .status = WriteStatus::failed,
      .accepted_frames = 0,
      .failure_category = PlaybackCategory::audio_output_failure,
  };

  static_assert(accepted.status == WriteStatus::accepted);
  static_assert(accepted.accepted_frames > 0);
  static_assert(would_block.status == WriteStatus::would_block);
  static_assert(would_block.accepted_frames == 0);
  static_assert(!would_block.failure_category.has_value());
  static_assert(failed.status == WriteStatus::failed);
  static_assert(failed.accepted_frames == 0);
  static_assert(failed.failure_category.has_value());
  static_assert(*failed.failure_category == PlaybackCategory::audio_output_failure);

  constexpr AudioDeviceSnapshot snapshot{
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

  static_assert(snapshot.device_instance_id == 9);
  static_assert(snapshot.snapshot_sequence == 4);
  static_assert(snapshot.accepted_frames_total == 960);
  static_assert(snapshot.device_consumed_frames_total == 480);
  static_assert(snapshot.device_queue_frames == 480);
  static_assert(snapshot.output_latency_frames.has_value());
  static_assert(*snapshot.output_latency_frames == 96);
  static_assert(snapshot.underrun_count == 1);
  static_assert(snapshot.discontinuity_count == 2);
  static_assert(snapshot.active);
  static_assert(accepted.accepted_frames != snapshot.accepted_frames_total);

  static_assert(AudioOutputOperations<shareme::core::AudioOutputDevice>);
  static_assert(AudioLifecycleOperations<shareme::core::AudioOutputDevice>);

  // A concrete output device/factory and runtime quiescence behavior are
  // deferred to the renderer/device tests in Task 2A.3.
}

}  // namespace

int main() {
  asserts_portable_value_types_and_audio_frame_units();
  asserts_write_and_snapshot_value_contracts_without_a_device_factory();
  return 0;
}
