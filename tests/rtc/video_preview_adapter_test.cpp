#include "video_preview_adapter.hpp"
#include "movie_video_playout_adapter.hpp"
#include "shareme/core/movie_audio_renderer.hpp"

#include <QGuiApplication>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"

namespace {

void require(bool condition, const char* expression, int line) {
  if (condition)
    return;
  std::cerr << "Requirement failed at line " << line << ": " << expression
            << '\n';
  std::exit(EXIT_FAILURE);
}

#define REQUIRE(expression) require((expression), #expression, __LINE__)

class ContinuityAudioOutput final : public shareme::core::AudioOutputDevice {
 public:
  shareme::core::OpenResult open(
      shareme::core::AudioOutputFormat) override {
    opened_ = true;
    return {.status = shareme::core::OpenStatus::opened};
  }

  bool start() override {
    active_ = opened_;
    return active_;
  }

  shareme::core::WriteResult try_write(
      shareme::core::AudioPcmBlockView pcm) override {
    if (!active_)
      return {.status = shareme::core::WriteStatus::failed,
              .failure_category =
                  shareme::core::PlaybackCategory::audio_output_device_lost};
    accepted_frames_ += pcm.frame_count;
    return {.status = shareme::core::WriteStatus::accepted,
            .accepted_frames = pcm.frame_count};
  }

  shareme::core::AudioDeviceSnapshot snapshot() override {
    return {.device_instance_id = 1,
            .snapshot_sequence = ++snapshot_sequence_,
            .accepted_frames_total = accepted_frames_,
            .device_consumed_frames_total = 0,
            .device_queue_frames = accepted_frames_,
            .active = active_};
  }

  shareme::core::FinalDeviceSnapshot quiesce_and_snapshot() override {
    active_ = false;
    const auto ordinary = snapshot();
    return {.device_instance_id = ordinary.device_instance_id,
            .snapshot_sequence = ordinary.snapshot_sequence,
            .accepted_frames_total = ordinary.accepted_frames_total,
            .device_consumed_frames_total = ordinary.device_consumed_frames_total,
            .device_queue_frames = ordinary.device_queue_frames,
            .active = false,
            .quiesced = true,
            .exact_consumption = true};
  }

  void pause() override { active_ = false; }
  void stop() override { active_ = false; }

 private:
  std::uint64_t accepted_frames_ = 0;
  std::uint64_t snapshot_sequence_ = 0;
  bool opened_ = false;
  bool active_ = false;
};

std::vector<std::byte> audio_bytes() {
  return std::vector<std::byte>(480U * 4U, std::byte{1});
}

shareme::core::AudioPcmBlockView audio_view(
    std::vector<std::byte>& bytes) {
  return {.frame_count = 480,
          .sample_rate = 48'000,
          .channel_count = 2,
          .sample_format = shareme::core::AudioSampleFormat::signed_int16,
          .interleaving = shareme::core::AudioInterleaving::interleaved,
          .payload = {.bytes = std::span<const std::byte>{bytes},
                      .frame_stride_bytes = 4}};
}

shareme::core::VideoClockInput locked_video_clock(
    std::int64_t audio_pts_ms, std::uint64_t observation_sequence,
    std::int64_t observation_time_ms) {
  return {.clock_confidence = shareme::core::ClockConfidence::locked,
          .audio_playout_pts_ms = audio_pts_ms,
          .playback_generation = 1,
          .route_generation = 1,
          .playing = true,
          .observation_sequence = observation_sequence,
          .observation_time_ms = observation_time_ms,
          .observed_video_pts_ms = -400};
}

webrtc::VideoFrame frame(std::uint32_t rtp_timestamp = 90) {
  auto buffer = webrtc::I420Buffer::Create(4, 4);
  for (int row = 0; row < 4; ++row)
    for (int column = 0; column < 4; ++column)
      buffer->MutableDataY()[row * buffer->StrideY() + column] =
          static_cast<std::uint8_t>(32 + row * 4 + column);
  return webrtc::VideoFrame::Builder()
      .set_video_frame_buffer(buffer)
      .set_timestamp_us(42)
       .set_rtp_timestamp(rtp_timestamp)
      .build();
}

void submits_planar_frame_and_keeps_one_in_flight(QGuiApplication& app) {
  QVideoSink sink;
  shareme::tools::VideoPreviewAdapter adapter(&sink);
  adapter.set_sink(&sink);
  const auto first = adapter.submit(frame());
  const auto second = adapter.submit(frame());
  REQUIRE(first.submitted);
  REQUIRE(first.path == shareme::tools::PreviewPath::planar_yuv);
  REQUIRE(second.path == shareme::tools::PreviewPath::coalesced);
  const auto pending = adapter.counters();
  REQUIRE(pending.pending_callbacks == 1);
  REQUIRE(pending.pending_callback_bytes > 0);
  REQUIRE(pending.pending_callback_bytes <=
          pending.peak_pending_callback_bytes);
  app.processEvents();
  const auto completed = adapter.counters();
  REQUIRE(completed.submissions == 1);
  REQUIRE(completed.coalesced == 1);
  REQUIRE(completed.max_pending_depth == 1);
  REQUIRE(completed.pending_callbacks == 0);
  REQUIRE(completed.pending_callback_bytes == 0);
  REQUIRE(sink.videoFrame().isValid());
  REQUIRE(sink.videoFrame().startTime() == 42);
  REQUIRE(sink.videoFrame().pixelFormat() ==
          QVideoFrameFormat::Format_YUV420P);
}

void rejects_without_sink(QGuiApplication& app) {
  Q_UNUSED(app);
  QVideoSink sink;
  shareme::tools::VideoPreviewAdapter adapter(&sink);
  const auto result = adapter.submit(frame());
  REQUIRE(!result.submitted);
  REQUIRE(result.path == shareme::tools::PreviewPath::no_sink);
}

void keeps_the_i420_buffer_alive_without_copying_planes(QGuiApplication& app) {
  QVideoSink sink;
  shareme::tools::VideoPreviewAdapter adapter(&sink);
  adapter.set_sink(&sink);
  auto source_buffer = webrtc::I420Buffer::Create(4, 4);
  source_buffer->MutableDataY()[0] = 32;
  const auto source = webrtc::VideoFrame::Builder()
                          .set_video_frame_buffer(source_buffer)
                          .set_timestamp_us(42)
                          .set_rtp_timestamp(90)
                          .build();
  const auto result = adapter.submit(source);
  REQUIRE(result.path == shareme::tools::PreviewPath::planar_yuv);
  source_buffer->MutableDataY()[0] = 220;
  app.processEvents();
  auto delivered = sink.videoFrame();
  REQUIRE(delivered.map(QVideoFrame::ReadOnly));
  REQUIRE(delivered.bits(0)[0] == 220);
  delivered.unmap();
}

void observational_movie_adapter_preserves_preview_delivery(QGuiApplication& app) {
  QVideoSink sink;
  shareme::tools::MovieVideoPlayoutAdapter adapter(&sink);
  adapter.set_sink(&sink);
  const auto result = adapter.submit(
      frame(), shareme::core::VideoFrameTiming{
                    .token = 1, .media_pts_ms = 100, .playback_generation = 0});
  REQUIRE(result.disposition == shareme::core::VideoFrameDisposition::pass_through);
  REQUIRE(result.preview.submitted);
  app.processEvents();
  REQUIRE(sink.videoFrame().isValid());
}

void policy_adapter_releases_held_payloads(QGuiApplication& app) {
  QVideoSink sink;
  shareme::tools::MovieVideoPlayoutAdapter adapter(
      &sink, shareme::core::MovieVideoPlayoutSchedulerConfig{
                 .apply_policy = true});
  adapter.set_sink(&sink);
  static_cast<void>(adapter.advance({
      .clock_confidence = shareme::core::ClockConfidence::locked,
      .audio_playout_pts_ms = 0,
      .playback_generation = 1,
      .route_generation = 1,
      .playing = true,
      .observation_sequence = 1}));
  const auto held = adapter.submit(
      frame(), shareme::core::VideoFrameTiming{
                    .token = 11, .media_pts_ms = 50,
                    .playback_generation = 1});
  REQUIRE(held.disposition == shareme::core::VideoFrameDisposition::hold);
  const auto released = adapter.advance({
      .clock_confidence = shareme::core::ClockConfidence::locked,
      .audio_playout_pts_ms = 25,
      .playback_generation = 1,
      .route_generation = 1,
      .playing = true,
      .observation_sequence = 2});
  REQUIRE(released.released.size() == 1);
  REQUIRE(released.released.front().token == 11);
  REQUIRE(released.released.front().disposition ==
          shareme::core::VideoFrameDisposition::present);
  app.processEvents();
  REQUIRE(sink.videoFrame().isValid());
}

void adapter_releases_older_tokens_before_current(QGuiApplication& app) {
  QVideoSink sink;
  shareme::tools::MovieVideoPlayoutAdapter adapter(
      &sink, shareme::core::MovieVideoPlayoutSchedulerConfig{
                 .apply_policy = true});
  adapter.set_sink(&sink);
  std::vector<std::uint32_t> submitted;
  adapter.set_submitted_callback(
      [&submitted](std::uint32_t timestamp) { submitted.push_back(timestamp); });
  static_cast<void>(adapter.advance({
      .clock_confidence = shareme::core::ClockConfidence::locked,
      .audio_playout_pts_ms = 0,
      .playback_generation = 1,
      .route_generation = 1,
      .playing = true,
      .observation_sequence = 1}));
  static_cast<void>(adapter.submit(
      frame(101), shareme::core::VideoFrameTiming{
                     .token = 1, .media_pts_ms = 50,
                     .playback_generation = 1}));
  static_cast<void>(adapter.submit(
      frame(102), shareme::core::VideoFrameTiming{
                     .token = 2, .media_pts_ms = 100,
                     .playback_generation = 1}));
  static_cast<void>(adapter.submit(
      frame(103), shareme::core::VideoFrameTiming{
                     .token = 3, .media_pts_ms = 200,
                     .playback_generation = 1}));
  const auto bound = adapter.submit(
      frame(104), shareme::core::VideoFrameTiming{
                     .token = 4, .media_pts_ms = 300,
                     .playback_generation = 1});
  REQUIRE(bound.disposition == shareme::core::VideoFrameDisposition::pass_through);
  app.processEvents();
  REQUIRE(!submitted.empty());
  REQUIRE(submitted.front() == 101);
}

void duplicate_tokens_are_replaced_by_adapter_tokens(QGuiApplication& app) {
  QVideoSink sink;
  shareme::tools::MovieVideoPlayoutAdapter adapter(
      &sink, shareme::core::MovieVideoPlayoutSchedulerConfig{
                 .apply_policy = true});
  adapter.set_sink(&sink);
  static_cast<void>(adapter.advance({
      .clock_confidence = shareme::core::ClockConfidence::locked,
      .audio_playout_pts_ms = 0,
      .playback_generation = 1,
      .route_generation = 1,
      .playing = true,
      .observation_sequence = 1}));
  const auto first = adapter.submit(
      frame(201), shareme::core::VideoFrameTiming{
                     .token = 77, .media_pts_ms = 50,
                     .playback_generation = 1});
  const auto duplicate = adapter.submit(
      frame(202), shareme::core::VideoFrameTiming{
                     .token = 77, .media_pts_ms = 60,
                     .playback_generation = 1});
  REQUIRE(first.token == 77);
  REQUIRE(duplicate.token != first.token);
  static_cast<void>(adapter.advance({
      .clock_confidence = shareme::core::ClockConfidence::locked,
      .audio_playout_pts_ms = 60,
      .playback_generation = 1,
      .route_generation = 1,
      .playing = true,
      .observation_sequence = 2}));
  app.processEvents();
}

void adapter_shutdown_does_not_submit_held_frames(QGuiApplication& app) {
  QVideoSink sink;
  std::vector<std::uint32_t> submitted;
  {
    shareme::tools::MovieVideoPlayoutAdapter adapter(
        &sink, shareme::core::MovieVideoPlayoutSchedulerConfig{
                   .apply_policy = true});
    adapter.set_sink(&sink);
    adapter.set_submitted_callback(
        [&submitted](std::uint32_t timestamp) { submitted.push_back(timestamp); });
    static_cast<void>(adapter.advance({
        .clock_confidence = shareme::core::ClockConfidence::locked,
        .audio_playout_pts_ms = 0,
        .playback_generation = 1,
        .route_generation = 1,
        .playing = true,
        .observation_sequence = 1}));
    REQUIRE(adapter.submit(
                frame(301), shareme::core::VideoFrameTiming{
                               .token = 91, .media_pts_ms = 50,
                               .playback_generation = 1})
                .disposition == shareme::core::VideoFrameDisposition::hold);
  }
  app.processEvents();
  REQUIRE(submitted.empty());
}

void explicit_shutdown_releases_held_tokens_once(QGuiApplication& app) {
  QVideoSink sink;
  std::vector<std::uint32_t> submitted;
  shareme::tools::MovieVideoPlayoutAdapter adapter(
      &sink, shareme::core::MovieVideoPlayoutSchedulerConfig{
                 .apply_policy = true});
  adapter.set_sink(&sink);
  adapter.set_submitted_callback(
      [&submitted](std::uint32_t timestamp) { submitted.push_back(timestamp); });
  static_cast<void>(adapter.advance(locked_video_clock(0, 1, 0)));
  REQUIRE(adapter.submit(
              frame(401), shareme::core::VideoFrameTiming{
                             .token = 101, .media_pts_ms = 50,
                             .playback_generation = 1})
              .disposition == shareme::core::VideoFrameDisposition::hold);
  adapter.close_ingress();
  adapter.shutdown();
  REQUIRE(adapter.scheduler_snapshot().held_token_count == 0);
  adapter.shutdown();
  app.processEvents();
  REQUIRE(submitted.empty());
}

void deterministic_video_stalls_keep_audio_continuous_and_bound_policy(
    QGuiApplication& app) {
  for (const auto stall_ms : {std::int64_t{500}, std::int64_t{2'000}}) {
    QVideoSink sink;
    shareme::tools::MovieVideoPlayoutAdapter policy_adapter(
        &sink, shareme::core::MovieVideoPlayoutSchedulerConfig{
                   .apply_policy = true});
    policy_adapter.set_sink(&sink);
    static_cast<void>(policy_adapter.advance(locked_video_clock(0, 1, 0)));
    for (const auto [token, pts] : std::vector<std::pair<std::uint64_t,
                                                           std::int64_t>>{
             {501, 50}, {502, 100}, {503, 200}}) {
      REQUIRE(policy_adapter.submit(
                  frame(static_cast<std::uint32_t>(token)),
                  shareme::core::VideoFrameTiming{
                      .token = token, .media_pts_ms = pts,
                      .playback_generation = 1})
                  .disposition == shareme::core::VideoFrameDisposition::hold);
    }
    REQUIRE(policy_adapter.scheduler_snapshot().held_token_count <= 3);

    bool candidate_observed = false;
    std::uint64_t observation_sequence = 2;
    for (std::int64_t elapsed_ms = 250; elapsed_ms <= stall_ms;
         elapsed_ms += 250) {
      static_cast<void>(policy_adapter.advance(
          locked_video_clock(elapsed_ms, observation_sequence++, elapsed_ms)));
      const auto snapshot = policy_adapter.scheduler_snapshot();
      REQUIRE(snapshot.held_token_count <= 3);
      candidate_observed = candidate_observed || snapshot.hard_resync_candidate;
    }
    REQUIRE(policy_adapter.scheduler_snapshot().candidate_observation_count > 0);
    REQUIRE(candidate_observed == (stall_ms == 2'000));

    shareme::tools::MovieVideoPlayoutAdapter observational_adapter(&sink);
    observational_adapter.set_sink(&sink);
    const auto blocked = observational_adapter.advance({
        .clock_confidence = shareme::core::ClockConfidence::provisional,
        .audio_playout_pts_ms = 0,
        .playback_generation = 1,
        .route_generation = 1,
        .playing = true,
        .observation_sequence = 1,
        .observation_time_ms = 0,
        .observed_video_pts_ms = -400});
    REQUIRE(std::find(blocked.events.begin(), blocked.events.end(),
                      shareme::core::VideoSchedulerEvent::clock_blocked) !=
            blocked.events.end());
    const auto pass_through = observational_adapter.submit(
        frame(601), shareme::core::VideoFrameTiming{
                       .token = 601, .media_pts_ms = -400,
                       .playback_generation = 1});
    REQUIRE(pass_through.disposition ==
            shareme::core::VideoFrameDisposition::pass_through);
    REQUIRE(observational_adapter.scheduler_snapshot().held_token_count == 0);
    REQUIRE(observational_adapter.scheduler_snapshot().applied_action ==
            shareme::core::VideoAppliedAction::pass_through);

    shareme::core::MovieAudioRenderer renderer{
        shareme::core::MovieAudioRendererConfig{
            .ready_capacity = 4,
            .in_flight_capacity = 4,
            .maximum_block_bytes = 480U * 4U,
            .output_format = {.sample_rate = 48'000,
                              .channel_count = 2,
                              .sample_format =
                                  shareme::core::AudioSampleFormat::signed_int16,
                              .interleaving =
                                  shareme::core::AudioInterleaving::interleaved}}};
    REQUIRE(renderer.activate_output(std::make_unique<ContinuityAudioOutput>())
                .status == shareme::core::ActivationStatus::activated);
    auto bytes = audio_bytes();
    for (std::uint64_t sequence = 1; sequence <= 4; ++sequence)
      REQUIRE(renderer.try_enqueue(audio_view(bytes), sequence).status ==
              shareme::core::EnqueueStatus::accepted);
    renderer.pump(shareme::core::MonotonicTime{});
    const auto before = renderer.snapshot();
    for (std::int64_t elapsed_ms = 250; elapsed_ms <= stall_ms;
         elapsed_ms += 250) {
      renderer.pump(shareme::core::MonotonicTime{} +
                    std::chrono::milliseconds{elapsed_ms});
      app.processEvents();
    }
    const auto after = renderer.snapshot();
    REQUIRE(after.media_frames_enqueued_total ==
            before.media_frames_enqueued_total);
    REQUIRE(after.discontinuity_count == before.discontinuity_count);
    REQUIRE(after.ready_block_count + after.in_flight_block_count <= 8);
  }
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  submits_planar_frame_and_keeps_one_in_flight(app);
  rejects_without_sink(app);
  keeps_the_i420_buffer_alive_without_copying_planes(app);
  observational_movie_adapter_preserves_preview_delivery(app);
  policy_adapter_releases_held_payloads(app);
  adapter_releases_older_tokens_before_current(app);
  duplicate_tokens_are_replaced_by_adapter_tokens(app);
  adapter_shutdown_does_not_submit_held_frames(app);
  explicit_shutdown_releases_held_tokens_once(app);
  deterministic_video_stalls_keep_audio_continuous_and_bound_policy(app);
  return EXIT_SUCCESS;
}
