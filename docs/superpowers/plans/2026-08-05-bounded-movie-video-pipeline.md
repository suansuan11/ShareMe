# Bounded Movie Video Pipeline and RSS Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bound every owned movie-media queue, preserve existing video quality and drop behavior, and emit per-second evidence that identifies the owner of host/viewer memory growth.

**Architecture:** Keep FFmpeg frames owned by the media layer and add a testable bounded `PendingMediaEvents` queue with separate video/audio capacities. Expose portable media and `PlaybackSession` snapshots, query video-only WebRTC stats only in diagnostic mode, and extend the existing one-callback Qt adapter and sanitized `PERF_COUNTERS` schema without changing normal playback.

**Tech Stack:** C++20, CMake/Ninja, FFmpeg/libswscale/libswresample, libyuv, libwebrtc RTC stats, Qt 6 Multimedia, Python 3, CTest.

## Global Constraints

- Work only in the existing ignored worktree `.worktrees/movie-playback-performance` on `codex/movie-playback-performance`; leave `main` unchanged.
- Do not lower source or transmitted dimensions, cadence, bitrate, codec quality, chroma quality, or color fidelity.
- Do not add a new intentional frame-drop policy.
- Preserve existing `PlaybackSession` video `drop_oldest` behavior and report every occurrence; do not hide or reinterpret an existing drop.
- Preserve PTS, generation, seek, pause/resume, EOS, audio, voice, and WebRTC transport semantics.
- Keep movie audio, voice, and video lifecycles and queues independent.
- Keep `client/core` free of Qt, FFmpeg, WebRTC, GPU SDK, and OS dependencies.
- Do not enable or redesign VideoToolbox, H.264, P010, or Windows hardware paths in this stage.
- Keep raw movie files, logs, JSONL, traces, build output, local paths, and the external libwebrtc cache out of Git.
- Diagnostic output must not contain paths, room identifiers, SDP, ICE addresses, credentials, or device identifiers.
- Write each behavior test before its production implementation and run the test to observe the expected failure.
- Preserve the two existing uncommitted source files until their changes are reviewed; incorporate only bounded, sanitized behavior into the focused commits.

---

## File Map

The implementation uses the following ownership boundaries:

- `client/core/include/shareme/core/bounded_queue.hpp`: portable generic queue capacity and optional byte accounting.
- `client/media/playback/include/shareme/media/media_frame.hpp`: media-owned vector-capacity byte helpers.
- `client/media/playback/include/shareme/media/media_source.hpp`: portable source metrics contract with a zero-valued default for existing fake sources.
- `client/media/playback/include/shareme/media/pending_media_events.hpp`: bounded FIFO for `MediaEvent` with per-kind limits and byte snapshots.
- `client/media/playback/include/shareme/media/playback_session.hpp` and `client/media/playback/src/playback_session.cpp`: source plus playback-queue snapshots.
- `client/media/playback/include/shareme/media/ffmpeg_media_source.hpp` and `client/media/playback/src/ffmpeg_media_source.cpp`: FFmpeg counters, bounded enqueue, and resumable decoder/resampler drain.
- `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp` and `client/rtc/movie/src/movie_video_source.cpp`: expose video session metrics and remove unsanitized diagnostic output.
- `client/tools/rtc_demo/video_preview_adapter.hpp` and `client/tools/rtc_demo/video_preview_adapter.cpp`: current Qt callback and pending-frame byte counters.
- `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp` and `client/rtc/webrtc/src/signaled_peer.cpp`: video-only outbound/inbound stats snapshots.
- `client/tools/rtc_demo/rtc_demo_controller.hpp` and `client/tools/rtc_demo/rtc_demo_controller.cpp`: aggregate role-specific counters and print sanitized lines once per second.
- `scripts/run_movie_performance_study.py` and `tests/scripts/movie_performance_study_test.py`: allowlist and parser coverage for the new fields.
- `tests/core/bounded_queue_test.cpp`, `tests/media/pending_media_events_test.cpp`, `tests/media/playback_session_test.cpp`, `tests/media/ffmpeg_media_source_test.cpp`, `tests/rtc/movie_video_source_test.cpp`, `tests/rtc/video_preview_adapter_test.cpp`, and `tests/rtc/signaled_peer_test.cpp`: RED/GREEN regression coverage.
- `docs/verification/movie-playback-performance.md` and `docs/development/current-stage.md`: stage evidence and handoff, updated only after implementation verification.

## Task 1: Add Generic Queue Byte Accounting

**Files:**
- Modify: `client/core/include/shareme/core/bounded_queue.hpp`
- Modify: `tests/core/bounded_queue_test.cpp`

**Interfaces:**
- Add `using ItemSize = std::size_t (*)(const T&) noexcept;` inside `BoundedQueue<T>`.
- Extend the constructor to `BoundedQueue(std::size_t capacity, OverflowPolicy policy, ItemSize item_size = nullptr)` while preserving both existing two-argument call sites.
- Add `std::size_t bytes() const`, `std::size_t peak_bytes() const`, and `std::size_t dropped_bytes() const`.

- [ ] **Step 1: Write the failing test for current and peak bytes.**

Add a local item type and size function to `tests/core/bounded_queue_test.cpp`:

```cpp
struct SizedItem {
  int value;
  std::size_t bytes;
};

std::size_t sized_item_bytes(const SizedItem& item) noexcept {
  return item.bytes;
}

void accounts_current_peak_and_dropped_bytes() {
  using shareme::core::BoundedQueue;
  using shareme::core::OverflowPolicy;

  BoundedQueue<SizedItem> queue{2, OverflowPolicy::drop_oldest,
                                &sized_item_bytes};
  REQUIRE(queue.push({1, 10}));
  REQUIRE(queue.push({2, 20}));
  REQUIRE(queue.bytes() == 30);
  REQUIRE(queue.peak_bytes() == 30);
  REQUIRE(queue.push({3, 40}));
  REQUIRE(queue.bytes() == 60);
  REQUIRE(queue.dropped_bytes() == 10);
  REQUIRE(queue.peak_bytes() == 60);
  REQUIRE(queue.pop()->value == 2);
  REQUIRE(queue.bytes() == 40);
  queue.clear();
  REQUIRE(queue.bytes() == 0);
  REQUIRE(queue.peak_bytes() == 60);
}
```

Call `accounts_current_peak_and_dropped_bytes()` from `main()`.

- [ ] **Step 2: Run the focused test and verify it fails for the missing API.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_bounded_queue_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^bounded_queue$'
```

Expected: compilation fails because the three-argument constructor and byte snapshot methods do not exist.

- [ ] **Step 3: Implement minimal accounting in `BoundedQueue`.**

Store `item_size_`, `bytes_`, `peak_bytes_`, and `dropped_bytes_`. In `push`, compute the incoming size before moving the item. When full with `drop_oldest`, compute the front item size before `pop_front()`, add that front size to `dropped_bytes_`, and subtract it from `bytes_`. When full with `reject_newest`, add the incoming size to `dropped_bytes_` and leave `bytes_` and the stored item unchanged. After insertion, add the incoming size and update `peak_bytes_`. In `pop`, subtract the front size before moving it. In `clear`, clear items and set only current bytes to zero; retain peak and cumulative drop counters.

Use a null size function as a zero-byte accounting mode so existing integer tests remain valid:

```cpp
[[nodiscard]] std::size_t item_bytes(const T& item) const noexcept {
  return item_size_ == nullptr ? 0 : item_size_(item);
}
```

- [ ] **Step 4: Run the focused test and verify it passes.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_bounded_queue_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^bounded_queue$'
```

Expected: `bounded_queue` passes, including the pre-existing zero-capacity, FIFO, drop, reject, and clear tests.

- [ ] **Step 5: Commit the isolated queue change.**

```bash
git add client/core/include/shareme/core/bounded_queue.hpp tests/core/bounded_queue_test.cpp
git commit -m "feat: account bounded media queue bytes"
```

## Task 2: Define Media and Playback Queue Snapshots

**Files:**
- Modify: `client/media/playback/include/shareme/media/media_frame.hpp`
- Modify: `client/media/playback/include/shareme/media/media_source.hpp`
- Modify: `client/media/playback/include/shareme/media/playback_session.hpp`
- Modify: `client/media/playback/src/playback_session.cpp`
- Modify: `tests/media/playback_session_test.cpp`

**Interfaces:**
- Add `std::size_t video_frame_capacity_bytes(const VideoFrame&) noexcept` and `std::size_t audio_frame_capacity_bytes(const AudioFrame&) noexcept` in `shareme::media`.
- Add `struct MediaSourceMetrics` with `decoded_video_frames`, `decoded_audio_frames`, `pending_events`, `pending_bytes`, `peak_pending_events`, `peak_pending_bytes`, and `backpressure_events`.
- Add a default `virtual MediaSourceMetrics metrics() const noexcept { return {}; }` to `IMediaSource` so existing fake sources remain source-compatible.
- Add `struct PlaybackSessionMetrics` with `MediaSourceMetrics source`, video/audio queue size/capacity/current bytes/peak bytes, and video/audio dropped counts.
- Add `[[nodiscard]] PlaybackSessionMetrics metrics() const noexcept` to `PlaybackSession`.

- [ ] **Step 1: Write the failing byte-helper and session snapshot tests.**

Extend the existing fake-frame setup in `tests/media/playback_session_test.cpp`:

```cpp
void reports_bounded_video_bytes() {
  auto source = std::make_unique<FakeMediaSource>();
  auto* observed_source = source.get();
  shareme::media::PlaybackSession session{std::move(source)};
  static_cast<void>(session.open("movie.mp4"));

  observed_source->push_video(1, FakeMediaSource::use_requested_generation);
  observed_source->push_video(2, FakeMediaSource::use_requested_generation);
  session.play();
  REQUIRE(wait_until([&session] {
    return session.metrics().video_queue_size > 0;
  }));

  const auto metrics = session.metrics();
  REQUIRE(metrics.video_queue_capacity == 3);
  REQUIRE(metrics.video_queue_size <= metrics.video_queue_capacity);
  REQUIRE(metrics.video_queue_bytes > 0);
  REQUIRE(metrics.video_queue_peak_bytes >= metrics.video_queue_bytes);
  REQUIRE(metrics.video_dropped_count == session.video_dropped_count());
}
```

Add direct assertions for a `VideoFrame` with a 4-byte vector and an `AudioFrame` with four `int16_t` samples so the helpers use vector capacity rather than only logical metadata.

- [ ] **Step 2: Run the focused test and verify it fails for missing snapshots.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_playback_session_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^playback_session$'
```

Expected: compilation fails because the media byte helpers and `PlaybackSession::metrics()` are not defined.

- [ ] **Step 3: Implement media byte helpers and queue wiring.**

Count vector capacities with saturating addition in the media layer so a malformed or adversarial frame cannot wrap the diagnostic byte total:

```cpp
inline std::size_t video_frame_capacity_bytes(const VideoFrame& frame) noexcept {
  const auto add = [](std::size_t lhs, std::size_t rhs) noexcept {
    return rhs > std::numeric_limits<std::size_t>::max() - lhs
               ? std::numeric_limits<std::size_t>::max()
               : lhs + rhs;
  };
  return add(add(add(frame.i420_y.capacity(), frame.i420_u.capacity()),
                 frame.i420_v.capacity()),
             frame.rgba.capacity());
}
```

Use the equivalent saturating multiplication for `interleaved_samples.capacity() * sizeof(std::int16_t)` for audio. Include `<limits>`. Pass these functions into the two `BoundedQueue` constructors in `PlaybackSession::Impl`. Add `metrics()` to `Impl`, taking the source snapshot under `source_mutex_` and reading the queue snapshots through their existing locks. Make `source_mutex_` mutable so the const snapshot method remains thread-safe.

- [ ] **Step 4: Run the focused test and verify it passes.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_playback_session_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^playback_session$'
```

Expected: all existing playback state, seek, drop, decode-ahead, and audio tests pass with the new byte assertions.

- [ ] **Step 5: Commit the portable session metrics.**

```bash
git add client/media/playback/include/shareme/media/media_frame.hpp client/media/playback/include/shareme/media/media_source.hpp client/media/playback/include/shareme/media/playback_session.hpp client/media/playback/src/playback_session.cpp tests/media/playback_session_test.cpp
git commit -m "feat: expose bounded playback queue metrics"
```

## Task 3: Add the Testable Pending Media Queue

**Files:**
- Create: `client/media/playback/include/shareme/media/pending_media_events.hpp`
- Create: `tests/media/pending_media_events_test.cpp`
- Modify: `client/media/playback/CMakeLists.txt`
- Modify: `tests/media/CMakeLists.txt`

**Interfaces:**
- Add `PendingMediaMetrics` with current total/video/audio counts, current bytes, peak count/bytes, and `backpressure_events`.
- Add `class PendingMediaEvents` with `video_capacity = 3`, `audio_capacity = 24`, `can_push_video()`, `can_push_audio()`, `bool push(MediaEvent&&)`, `std::optional<MediaEvent> pop()`, `clear()`, `empty()`, and `metrics()`.
- `push(MediaEvent&&)` must return `false` without consuming its argument when the corresponding per-kind capacity is full.

- [ ] **Step 1: Register the new test and write RED queue behavior tests.**

Create `tests/media/pending_media_events_test.cpp` with tests for both kinds:

```cpp
void rejects_video_without_consuming_when_full() {
  shareme::media::PendingMediaEvents events;
  for (int value = 0; value < 3; ++value)
    REQUIRE(events.push(video_event(value)));

  auto fourth = video_event(3);
  REQUIRE_FALSE(events.push(std::move(fourth)));
  REQUIRE(fourth.index() == 0);
  REQUIRE(events.metrics().video_size == 3);
  REQUIRE(events.metrics().backpressure_events == 1);
}
```

Add FIFO, mixed audio/video per-kind capacity, current/peak bytes, and clear-current-bytes tests. `video_event()` must construct a valid I420 frame with a distinct PTS; `audio_event()` must construct a valid stereo sample vector.

- [ ] **Step 2: Run the new test and verify the expected missing-header failure.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_pending_media_events_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^pending_media_events$'
```

Expected: the target cannot compile because `PendingMediaEvents` does not exist.

- [ ] **Step 3: Implement `PendingMediaEvents` with explicit per-kind accounting.**

Use an internal `std::deque<MediaEvent>`, `video_size_`, `audio_size_`, and `bytes_`. Determine the kind and byte size before modifying the deque. On a full kind, increment `backpressure_events_` and return `false` without moving the argument. On push, update current and peak counts/bytes. On pop, subtract the front event's byte size before moving it. On clear, reset current counts/bytes but retain peak and cumulative backpressure values.

- [ ] **Step 4: Run the new test and verify it passes.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_pending_media_events_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^pending_media_events$'
```

Expected: all per-kind capacity, no-consume-on-full, FIFO, byte, peak, and clear tests pass.

- [ ] **Step 5: Commit the pending queue component.**

```bash
git add client/media/playback/include/shareme/media/pending_media_events.hpp client/media/playback/CMakeLists.txt tests/media/pending_media_events_test.cpp tests/media/CMakeLists.txt
git commit -m "feat: bound pending media events"
```

## Task 4: Integrate FFmpeg Backpressure and Resumable Flush

**Files:**
- Modify: `client/media/playback/include/shareme/media/ffmpeg_media_source.hpp`
- Modify: `client/media/playback/src/ffmpeg_media_source.cpp`
- Modify: `tests/media/ffmpeg_media_source_test.cpp`

**Interfaces:**
- Add `[[nodiscard]] MediaSourceMetrics metrics() const noexcept override` to `FfmpegMediaSource`.
- Replace the private raw pending deque with `PendingMediaEvents pending_events_`.
- Add private `DrainResult drain(AVCodecContext*, bool, std::uint64_t)` where `DrainResult` distinguishes `exhausted` from `pending_full`.

- [ ] **Step 1: Write RED FFmpeg metric and EOS tests.**

Extend `tests/media/ffmpeg_media_source_test.cpp` to count every video/audio event to EOS and assert bounded snapshots after every read:

```cpp
void pending_metrics_stay_bounded_and_tail_is_present(
    const std::filesystem::path& movie_path) {
  shareme::media::FfmpegMediaSource source;
  static_cast<void>(source.open(movie_path));
  std::size_t event_count = 0;
  while (true) {
    const auto metrics = source.metrics();
    REQUIRE(metrics.pending_events <= 27);
    REQUIRE(metrics.pending_bytes <= metrics.peak_pending_bytes);
    const auto event = source.read_next(23);
    if (std::holds_alternative<shareme::media::EndOfStream>(event))
      break;
    ++event_count;
  }
  REQUIRE(event_count > 0);
  REQUIRE(source.metrics().pending_events == 0);
}
```

Add a fixture-specific expected video/audio count using the existing generated one-second media fixture, so the test proves EOS does not discard events while a bounded drain is active. Add a seek assertion that pending bytes return to zero before the next generation's event.

- [ ] **Step 2: Run the FFmpeg test and verify it fails for the missing metrics and bounded implementation.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_ffmpeg_media_source_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^ffmpeg_media_source$'
```

Expected: compilation fails for `FfmpegMediaSource::metrics()` and the new bounded assertions.

- [ ] **Step 3: Replace the source pending deque and add source counters.**

Include `pending_media_events.hpp`, increment `decoded_video_frames` or `decoded_audio_frames` immediately after a successful `avcodec_receive_frame()`, and enqueue through `PendingMediaEvents`. Remove the uncommitted high-frequency `std::cout` drain logging. Expose the pending queue snapshot through atomic or synchronized source metrics; `metrics()` may copy the current queue metrics because the source mutex already serializes source access, while cumulative decoder counts use atomics if queried outside that mutex.

- [ ] **Step 4: Make normal decoder drain stop before capacity and resume later.**

Before calling `avcodec_receive_frame`, check `can_push_video()` or `can_push_audio()`. If false, increment the queue backpressure counter and return `pending_full`. Convert and enqueue only after capacity is available. Never create a `VideoFrame` that will be rejected by the pending queue.

- [ ] **Step 5: Fix flush and resampler state transitions.**

Add separate `video_flush_sent_`, `audio_flush_sent_`, `video_flush_complete_`, `audio_flush_complete_`, and `resampler_drained_` flags. Set a decoder complete flag only after `avcodec_receive_frame()` returns `EAGAIN` or `EOF`, not when a capacity check stops the loop. Make `flush_decoders()` revisit incomplete decoders after pending events are popped. In `drain_resampler()`, leave `resampler_drained_` false when audio capacity is full and resume on the next call. Return `EndOfStream` only when every enabled decoder and the resampler is complete and `pending_events_` is empty.

- [ ] **Step 6: Reset seek/close state without losing current metrics.**

Use `pending_events_.clear()` in `seek()` and `close()`, reset all decoder and resampler state flags, and preserve cumulative peak/backpressure counters until a new `open()` begins a new source lifetime. Ensure every `av_frame_unref`, `av_packet_unref`, and hardware transfer error path remains intact.

- [ ] **Step 7: Run the FFmpeg test and verify it passes.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_ffmpeg_media_source_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^ffmpeg_media_source$'
```

Expected: generated, video-only, audio-only, seek, resampler, fallback, and full-EOS tests pass without a pending count above 27.

- [ ] **Step 8: Commit the FFmpeg bounded pipeline.**

```bash
git add client/media/playback/include/shareme/media/ffmpeg_media_source.hpp client/media/playback/src/ffmpeg_media_source.cpp tests/media/ffmpeg_media_source_test.cpp
git commit -m "fix: make ffmpeg drain resumable and bounded"
```

## Task 5: Expose Movie Video Session Metrics and Remove Raw Diagnostics

**Files:**
- Modify: `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp`
- Modify: `client/rtc/movie/src/movie_video_source.cpp`
- Modify: `tests/rtc/movie_video_source_test.cpp`

**Interfaces:**
- Add `[[nodiscard]] media::PlaybackSessionMetrics playback_metrics() const noexcept` to `MovieVideoSource`.
- Keep `generated_count()` as the cumulative frames offered to WebRTC; do not relabel it as FFmpeg decoded count.

- [ ] **Step 1: Write the failing source snapshot test.**

After the existing `decodes_and_paces_movie_frames` wait loop, add:

```cpp
const auto playback = source->playback_metrics();
REQUIRE(playback.source.pending_events <= 3);
REQUIRE(playback.video_queue_size <= playback.video_queue_capacity);
REQUIRE(playback.video_queue_bytes <= playback.video_queue_peak_bytes);
REQUIRE(playback.source.decoded_video_frames >= sink.frame_count());
```

- [ ] **Step 2: Run the focused movie source test and verify the missing method failure.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_movie_video_source_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^movie_video_source$'
```

Expected: compilation fails because `playback_metrics()` is not defined.

- [ ] **Step 3: Implement the snapshot and sanitize the existing worktree diagnostics.**

Return `session_ ? session_->metrics() : media::PlaybackSessionMetrics{}`. Keep the method safe before start and after stop. Remove the existing uncommitted movie path/open-failure `std::cout` lines; retain typed `set_error()` categories. Do not add replacement logs outside `PERF_COUNTERS`.

- [ ] **Step 4: Run the focused movie source test and verify it passes.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_movie_video_source_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^movie_video_source$'
```

Expected: movie pacing, seek, pause, generation, stop, and new metrics assertions pass.

- [ ] **Step 5: Commit the movie source integration.**

```bash
git add client/rtc/movie/include/shareme/rtc/movie_video_source.hpp client/rtc/movie/src/movie_video_source.cpp tests/rtc/movie_video_source_test.cpp
git commit -m "feat: expose movie video queue metrics"
```

## Task 6: Add Qt Pending Callback and Buffer Metrics

**Files:**
- Modify: `client/tools/rtc_demo/video_preview_adapter.hpp`
- Modify: `client/tools/rtc_demo/video_preview_adapter.cpp`
- Modify: `tests/rtc/video_preview_adapter_test.cpp`

**Interfaces:**
- Extend `VideoPreviewCounters` with `pending_callbacks`, `pending_callback_bytes`, and `peak_pending_callback_bytes`.
- Keep `VideoPreviewAdapter::submit(const webrtc::VideoFrame&)` and existing `PreviewPath` values unchanged.

- [ ] **Step 1: Write RED assertions for current callback and bytes.**

In `submits_planar_frame_and_keeps_one_in_flight`, assert before `processEvents()`:

```cpp
const auto pending = adapter.counters();
REQUIRE(pending.pending_callbacks == 1);
REQUIRE(pending.pending_callback_bytes > 0);
REQUIRE(pending.pending_callback_bytes <= pending.peak_pending_callback_bytes);
```

After `app.processEvents()`, assert `pending_callbacks == 0` and `pending_callback_bytes == 0`. Keep the existing coalesced second submission assertions.

- [ ] **Step 2: Run the focused preview test and verify the missing fields failure.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_video_preview_adapter_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^video_preview_adapter$'
```

Expected: compilation fails because `VideoPreviewCounters` does not expose the new fields.

- [ ] **Step 3: Track pending planar/fallback bytes at the single callback gate.**

Add an `owned_bytes` field to `PreparedFrame`. For planar I420, compute `StrideY() * height + StrideU() * chroma_height + StrideV() * chroma_height`; for the ARGB fallback, use the copied frame's `sizeInBytes()`. Set `pending_callback_bytes` before queuing the lambda, update the peak with an atomic max, and clear current bytes immediately after sink submission or queued-call rejection. Keep `pending_callbacks` exactly 0/1 and keep the existing `pending.exchange(true)` coalescing behavior.

- [ ] **Step 4: Run the focused preview test and verify it passes.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_video_preview_adapter_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^video_preview_adapter$'
```

Expected: planar lifetime, one-in-flight, no-sink, fallback, timestamp, and byte snapshot tests pass.

- [ ] **Step 5: Commit the Qt metrics.**

```bash
git add client/tools/rtc_demo/video_preview_adapter.hpp client/tools/rtc_demo/video_preview_adapter.cpp tests/rtc/video_preview_adapter_test.cpp
git commit -m "feat: expose bounded preview callback metrics"
```

## Task 7: Add Video-Only WebRTC Stats Snapshots

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`

**Interfaces:**
- Add `struct SignaledVideoStats` with optional `frames_encoded`, `frames_sent`, `frames_received`, `frames_decoded`, `frames_dropped`, and `bool unavailable`.
- Add `[[nodiscard]] SignaledVideoStats video_stats() const noexcept` to `SignaledPeer`.

- [ ] **Step 1: Write the failing directional stats test.**

In the existing send-only/receive-only peer test, after both `wait()` futures return and before stopping either peer, add:

```cpp
const auto host_video_stats = send_only_host->video_stats();
const auto viewer_video_stats = receive_only_viewer->video_stats();
REQUIRE(!host_video_stats.unavailable);
REQUIRE(host_video_stats.frames_encoded.has_value());
REQUIRE(*host_video_stats.frames_encoded > 0);
REQUIRE(!viewer_video_stats.unavailable);
REQUIRE(viewer_video_stats.frames_received.has_value());
REQUIRE(*viewer_video_stats.frames_received > 0);
REQUIRE(viewer_video_stats.frames_decoded.has_value());
REQUIRE(*viewer_video_stats.frames_decoded > 0);
```

- [ ] **Step 2: Run the signaling test and verify the missing API failure.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_signaled_peer_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^signaled_peer$'
```

Expected: compilation fails because `video_stats()` and `SignaledVideoStats` do not exist.

- [ ] **Step 3: Implement a video-only stats query on the signaling thread.**

Reuse `StatsObserver` and `GetStats`. In `SignaledPeer::Impl::video_stats()`, return `unavailable = true` when the runtime, signaling thread, stats report, or matching fields are absent. Iterate only `RTCOutboundRtpStreamStats` and `RTCInboundRtpStreamStats` whose `kind` is `"video"`; copy the optional frame counters without touching audio stats. Execute the query through `runtime_->signaling_thread()->BlockingCall` and catch failures into `unavailable = true` rather than propagating into the media call.

- [ ] **Step 4: Run the signaling test and verify it passes.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_signaled_peer_test --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^signaled_peer$'
```

Expected: the directional send-only/receive-only lifecycle and video stats assertions pass, with the existing audio/control/preview assertions unchanged.

- [ ] **Step 5: Commit the WebRTC stats boundary.**

```bash
git add client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp client/rtc/webrtc/src/signaled_peer.cpp tests/rtc/signaled_peer_test.cpp
git commit -m "feat: expose diagnostic video codec stats"
```

## Task 8: Emit Sanitized Host and Viewer Diagnostics

**Files:**
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `scripts/run_movie_performance_study.py`
- Modify: `tests/scripts/movie_performance_study_test.py`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Extend the existing `PERF_COUNTERS version=1` allowlist with `source_pending`, `source_pending_bytes`, `source_peak_pending`, `source_peak_pending_bytes`, `session_video_pending`, `session_video_bytes`, `session_audio_pending`, `session_audio_bytes`, `render_queue`, `pending_callbacks`, `pending_callback_bytes`, `owned_bytes`, `owned_peak_bytes`, `backpressure_events`, and `stats_unavailable`.
- Keep all new values non-negative integers; retain existing enum and metadata sanitization.

- [ ] **Step 1: Write RED parser tests for every new field.**

Extend `test_parses_only_sanitized_performance_counter_lines` with a line containing all new fields:

```python
line = (
    "PERF_COUNTERS version=1 role=host cpu_percent=1 rss_bytes=2 "
    "decoded=3 offered=4 encoded=5 received=6 callback=7 submitted=8 "
    "coalesced=0 dropped=0 conversion_failures=0 fallback_copies=0 "
    "max_pending=1 source_pending=2 source_pending_bytes=30 "
    "source_peak_pending=3 source_peak_pending_bytes=40 "
    "session_video_pending=1 session_video_bytes=20 "
    "session_audio_pending=0 session_audio_bytes=0 render_queue=1 "
    "pending_callbacks=1 pending_callback_bytes=10 owned_bytes=60 "
    "owned_peak_bytes=70 backpressure_events=2 stats_unavailable=0 "
    "width=3840 height=2160 cadence_num=24000 cadence_den=1001 "
    "pixel_aspect_num=1 pixel_aspect_den=1 color_range=limited "
    "color_space=unknown codec=hevc profile=main10 path=software "
    "state=playing candidate=host"
)
parsed = self.runner.parse_perf_counters(line)
self.assertEqual(parsed["source_pending_bytes"], 30)
self.assertEqual(parsed["pending_callbacks"], 1)
self.assertEqual(parsed["owned_peak_bytes"], 70)
```

Add negative-value rejection for `source_pending_bytes` and a path-bearing rejection for an unknown field. Add a `rtc_demo_cli` contract assertion that diagnostics remain opt-in and `--video-acceleration software` remains accepted.

- [ ] **Step 2: Run the script tests and verify the allowlist failure.**

Run:

```bash
python3 -m unittest tests/scripts/movie_performance_study_test.py tests/scripts/rtc_demo_cli_test.py
```

Expected: the new parser case returns `None` because the fields are not yet allowlisted.

- [ ] **Step 3: Add role-specific metric aggregation in `RtcDemoController`.**

When the performance timer fires, gather `MovieVideoSource::playback_metrics()`, `VideoPreviewAdapter::counters()`, and `peer_->video_stats()` only if `performance_counters_enabled_` is true. Use these meanings:

```text
host decoded = source.decoded_video_frames
host offered = movie_video_source_->generated_count()
viewer decoded/received/dropped = inbound WebRTC stats when available
host encoded = outbound WebRTC stats when available
callback/submitted/coalesced = preview adapter and existing callback atomics
render_queue/pending_callbacks = adapter current pending callback count
```

Compute `owned_bytes` as the current sum of source pending bytes, session queue bytes, and preview pending bytes. Compute `owned_peak_bytes` as the conservative sum of the corresponding component peaks. Set `stats_unavailable` to 1 whenever the video stats snapshot is unavailable. Omit unavailable optional `encoded`, `received`, or viewer `decoded` fields instead of printing fake zeros; keep the fixed sanitized fields present.

Build the line from allowlisted numeric and enum values only. Do not print `movie_path_`, room IDs, raw exception text, SDP, ICE, or arbitrary strings. Remove any remaining high-frequency FFmpeg/movie diagnostic output.

- [ ] **Step 4: Update the runner allowlist and preserve all existing gates.**

Add the new names to `ALLOWED_KEYS` and `INTEGER_KEYS` in `scripts/run_movie_performance_study.py`. Do not alter `gates_pass`, run count, process RSS sampling, quality thresholds, or artifact containment. The parser must continue accepting older valid version-1 lines that omit the new optional fields.

- [ ] **Step 5: Run script tests and the CLI contract.**

Run:

```bash
python3 -m unittest tests/scripts/movie_performance_study_test.py tests/scripts/rtc_demo_cli_test.py
```

Expected: all parser, aggregation, sanitization, command, and frozen-gate tests pass.

- [ ] **Step 6: Build and run a local counter smoke check.**

Run:

```bash
cmake --build build/movie-call-dev --target shareme_rtc_demo --parallel 4
QT_QPA_PLATFORM=offscreen SHAREME_PERFORMANCE_COUNTERS=1 build/movie-call-dev/client/tools/rtc_demo/shareme_rtc_demo --validate --server ws://127.0.0.1:18080/v1/ws --role host --source movie --movie "$MOVIE_PATH" --video-acceleration software
```

Expected: validation exits successfully without opening a call; no source path is printed by the validation process. Use the existing native movie-call command for live counter capture, not this validation-only invocation.

- [ ] **Step 7: Commit the sanitized diagnostic contract.**

```bash
git add client/tools/rtc_demo/rtc_demo_controller.hpp client/tools/rtc_demo/rtc_demo_controller.cpp scripts/run_movie_performance_study.py tests/scripts/movie_performance_study_test.py tests/scripts/rtc_demo_cli_test.py
git commit -m "feat: report bounded movie pipeline diagnostics"
```

## Task 9: Run Focused and Full Regression Verification

**Files:**
- No source files are added in the first verification pass.
- Modify only evidence documents after the verification commands produce fresh results.

- [ ] **Step 1: Run the affected CTest group.**

Run:

```bash
cmake --build build/movie-call-dev --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure -R '^(bounded_queue|pending_media_events|ffmpeg_media_source|playback_session|video_preview_adapter|movie_video_source|signaled_peer)$'
```

Expected: every affected test passes; the command output provides the exact count for the handoff.

- [ ] **Step 2: Run the complete macOS CTest suite.**

Run:

```bash
ctest --test-dir build/movie-call-dev --output-on-failure
```

Expected: all configured tests pass, including generated media fixtures, CLI contracts, quality contracts, signaling, movie audio, and the new pending queue target.

- [ ] **Step 3: Run repository checks without touching the external cache.**

Run:

```bash
git diff --check
git status --short --branch
git diff --stat
```

Inspect the status and diff to ensure only intended source/tests/docs are present. Do not delete or rewrite `/Users/dio/Library/Caches/ShareMe/webrtc`; use it read-only for the build.

- [ ] **Step 4: Run one diagnostic 180-second software session when the native inputs are available.**

Set `MOVIE_PATH` to the supplied movie, use the existing local signaling server root and demo path from the current verification environment, and run the already tested runner with `--video-acceleration software`:

```bash
python3 scripts/run_movie_performance_study.py \
  --output-root "$PERF_OUTPUT_ROOT" \
  --output-parent "$PERF_OUTPUT_PARENT" \
  --run-count 3 \
  --demo build/movie-call-dev/client/tools/rtc_demo/shareme_rtc_demo \
  --server-url ws://127.0.0.1:18080/v1/ws \
  --server-root "$SERVER_ROOT" \
  --movie "$MOVIE_PATH" \
  --video-acceleration software \
  --duration-seconds 180
```

Expected: three sequential complete artifacts with per-second host/viewer counters. Inspect `source_pending`, `session_video_pending`, `pending_callbacks`, `owned_bytes`, `backpressure_events`, and process RSS at elapsed seconds 0, 60, 120, and 180. Confirm every queue remains within its declared bound and no source-level drop counter increases. If the supplied movie or native runtime is unavailable, record the run as environment-dependent rather than fabricating evidence.

- [ ] **Step 5: Record only sanitized aggregate evidence.**

Update `docs/verification/movie-playback-performance.md` with platform, build identity, exact test counts, queue maxima, owned-byte maxima, counter availability, and the result category. Do not commit raw JSONL, paths, traces, or a claim of lower physical temperature. Keep the historical quality failure and hardware rejection unchanged.

- [ ] **Step 6: Update the dynamic handoff at the stage boundary.**

Update `docs/development/current-stage.md` only after all required checks have run. Use exactly one outcome label from the approved contract: `verified-performance-and-quality`, `partial-evidence`, or `blocked-on-quality-preserving-boundary`. Keep Windows, human visual/audio, display scanout, and physical thermal evidence environment-dependent when they were not run.

- [ ] **Step 7: Commit evidence and handoff separately.**

```bash
git add docs/verification/movie-playback-performance.md docs/development/current-stage.md
git commit -m "docs: record bounded movie pipeline evidence"
```

## Final Review Checklist

- [ ] No production code was retained without a test that failed before the implementation.
- [ ] FFmpeg pending events, `PlaybackSession` queues, Qt callback delivery, and WebRTC stats have distinct ownership and counters.
- [ ] Decoder flush and resampler drain resume after a full queue and do not lose tail events.
- [ ] Existing `drop_oldest` behavior is visible and no new source-level drop policy exists.
- [ ] Dimensions, cadence, color metadata, PTS, generation, audio, and directionality remain unchanged.
- [ ] Normal runs do not poll WebRTC stats or emit debug path logs.
- [ ] Sanitized parser fields reject negative, duplicate, unknown, and sensitive values.
- [ ] A fresh macOS CTest result, `git diff --check`, and exact platform/evidence labels are recorded.
- [ ] The external libwebrtc cache remains preserved and unstaged.
