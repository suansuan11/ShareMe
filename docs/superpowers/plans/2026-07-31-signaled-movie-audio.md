# Signaled Movie Audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Send real 48 kHz stereo movie audio on an independent WebRTC track while preserving bidirectional processed voice and sender-side A/V timing.

**Architecture:** FFmpeg opens independent video-only and audio-only decoders for the same host file. Both adapters share one monotonic `MovieTimeline` and pace against the container start PTS; the audio adapter rechunks decoded PCM into exact 10 ms frames and publishes them through a custom unprocessed `AudioSourceInterface`.

**Tech Stack:** C++20, CMake 3.25+, FFmpeg 8, libwebrtc, Qt 6, Go signaling, CTest, Python smoke orchestration

---

### Task 1: Audio-only FFmpeg and fixed PCM chunks

**Files:**
- Modify: `client/media/playback/include/shareme/media/ffmpeg_media_source.hpp`
- Modify: `client/media/playback/include/shareme/media/media_source.hpp`
- Modify: `client/media/playback/src/ffmpeg_media_source.cpp`
- Create: `client/media/playback/include/shareme/media/pcm_chunker.hpp`
- Create: `client/media/playback/src/pcm_chunker.cpp`
- Modify: `client/media/playback/CMakeLists.txt`
- Modify: `tests/media/ffmpeg_media_source_test.cpp`
- Create: `tests/media/pcm_chunker_test.cpp`
- Modify: `tests/media/CMakeLists.txt`

- [x] **Step 1: Write failing FFmpeg stream-selection tests**

Extend the FFmpeg test to open an A/V fixture in audio-only mode and an
audio-only fixture:

```cpp
FfmpegMediaSource source({
    .decode_video = false,
    .decode_audio = true,
});
const auto info = source.open(path);
REQUIRE(!info.has_video);
REQUIRE(info.has_audio);
REQUIRE(info.start_time_ms <= first_audio_pts_ms);
```

Add a generated video-only fixture and require
`AudioStreamUnavailable` in audio-only mode. Retain the existing default and
video-only cases.

- [x] **Step 2: Run the focused test and verify RED**

Run:

```bash
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev -R ffmpeg_media_source --output-on-failure
```

Expected: compilation fails because `decode_video`, `start_time_ms`, and
`AudioStreamUnavailable` do not exist.

- [x] **Step 3: Implement independent decoder selection and container origin**

Use these public contracts:

```cpp
struct MediaInfo {
  std::int64_t duration_ms{0};
  std::int64_t start_time_ms{0};
  bool has_video{false};
  bool has_audio{false};
  int video_width{0};
  int video_height{0};
};

class AudioStreamUnavailable final : public std::runtime_error {
public:
  AudioStreamUnavailable()
      : std::runtime_error{"Media file has no decodable audio stream"} {}
};

struct FfmpegMediaSourceOptions {
  bool decode_video{true};
  bool decode_audio{true};
};
```

Reject both options false. Open only requested decoders, ignore packets for
disabled streams, choose the enabled stream for seeking, flush only non-null
decoders, and accept either decoder in `ensure_open()`. Convert
`format_context_->start_time` from `AV_TIME_BASE_Q` to milliseconds, using zero
only for `AV_NOPTS_VALUE`.

- [x] **Step 4: Run FFmpeg and playback regressions**

Run:

```bash
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev -R 'ffmpeg_media_source|playback_session|playback_media_smoke' --output-on-failure
```

Expected: all selected tests pass.

- [x] **Step 5: Write failing PCM chunker tests**

Define the wished-for API in the test:

```cpp
PcmChunker chunker;
chunker.push(AudioFrame{
    .interleaved_samples = samples,
    .sample_rate = 48'000,
    .channels = 2,
    .pts_ms = 1'000,
});
auto chunk = chunker.pop();
REQUIRE(chunk->interleaved_samples.size() == 480 * 2);
REQUIRE(chunk->pts_ms == 1'000);
```

Cover sample order across two decoded inputs, a retained partial frame,
monotonic 10 ms PTS increments, invalid format rejection, and resetting pending
samples when the next input PTS differs from the expected position by more than
10 ms.

- [x] **Step 6: Run the chunker test and verify RED**

Run:

```bash
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev -R pcm_chunker --output-on-failure
```

Expected: configuration or compilation fails because `PcmChunker` is missing.

- [x] **Step 7: Implement the bounded 10 ms chunker**

Use:

```cpp
struct PcmChunk {
  std::vector<std::int16_t> interleaved_samples;
  std::int64_t pts_ms{0};
};

class PcmChunker {
public:
  [[nodiscard]] bool push(AudioFrame frame);
  [[nodiscard]] std::optional<PcmChunk> pop();
  void reset() noexcept;
  [[nodiscard]] std::string error() const;
};
```

Accept exactly 48 kHz stereo interleaved input, cap pending storage at 4,800
samples per channel, emit 480 samples per channel, and compute each chunk PTS
from the pending first-sample PTS and emitted sample count. On a discontinuity,
clear pending samples before accepting the new frame.

- [x] **Step 8: Verify and commit**

Run:

```bash
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev --output-on-failure
git diff --check
```

Expected: playback suite passes with the new chunker test.

Commit:

```bash
git add client/media/playback tests/media
git commit -m "feat: decode fixed movie audio chunks"
```

### Task 2: Shared movie timeline and WebRTC audio source

**Files:**
- Create: `client/rtc/webrtc/include/shareme/rtc/local_audio_source.hpp`
- Create: `client/rtc/movie/include/shareme/rtc/movie_timeline.hpp`
- Create: `client/rtc/movie/src/movie_timeline.cpp`
- Create: `client/rtc/movie/include/shareme/rtc/movie_audio_source.hpp`
- Create: `client/rtc/movie/src/movie_audio_source.cpp`
- Modify: `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp`
- Modify: `client/rtc/movie/src/movie_video_source.cpp`
- Modify: `client/rtc/movie/CMakeLists.txt`
- Create: `tests/rtc/movie_audio_source_test.cpp`
- Modify: `tests/rtc/movie_video_source_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

- [x] **Step 1: Write failing shared-timeline and movie-audio tests**

Generate an A/V fixture with audio starting at 4 seconds and video at 5
seconds. Create one timeline and both sources:

```cpp
auto timeline = std::make_shared<MovieTimeline>();
auto audio = MovieAudioSource::create(movie_path, timeline);
CountingPcmSink sink;
audio->AddSink(&sink);
REQUIRE(audio->start());
REQUIRE(sink.wait_for_frames(100, 3s));
REQUIRE(sink.sample_rate() == 48'000);
REQUIRE(sink.channels() == 2);
REQUIRE(sink.frames_per_callback() == 480);
REQUIRE(sink.peak() > 0);
```

Test missing files, a video-only file, prompt stop during a 5-second PTS gap,
and that the latest audio/video emitted PTS values differ by at most 50 ms once
both sources have emitted for two seconds.

- [x] **Step 2: Run focused tests and verify RED**

Run:

```bash
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev -R 'movie_audio_source|movie_video_source' --output-on-failure
```

Expected: compilation fails because the timeline and movie-audio source do not
exist.

- [x] **Step 3: Implement the source contracts and shared clock**

`LocalAudioSource` extends `webrtc::AudioSourceInterface` with:

```cpp
virtual bool start() = 0;
virtual void stop() noexcept = 0;
virtual std::uint64_t generated_count() const noexcept = 0;
virtual std::optional<std::int64_t> last_pts_ms() const noexcept = 0;
virtual std::string error() const = 0;
```

`MovieTimeline::start()` stores one `steady_clock::time_point` under a mutex and
returns it on every call. Add the same optional `last_pts_ms()` metric to
`LocalVideoSource`.

- [x] **Step 4: Implement movie-audio pacing and sink fan-out**

`MovieAudioSource` opens:

```cpp
FfmpegMediaSourceOptions{
    .decode_video = false,
    .decode_audio = true,
}
```

Use `PlaybackSession`, `PcmChunker`, and the shared epoch. Set the decode
playhead to `container_start_time_ms + elapsed_ms`; schedule each chunk at
`epoch + (chunk.pts_ms - container_start_time_ms)`. Publish callbacks with:

```cpp
sink->OnData(samples.data(), 16, 48'000, 2, 480,
             webrtc::TimeMillis());
```

Keep sink registration thread-safe, use unprocessed `AudioOptions`, sanitize
all exceptions, make start/stop idempotent, and interrupt pending waits before
joining.

- [x] **Step 5: Move movie video to the same origin**

Accept the shared timeline in `MovieVideoSource::create()`. Replace its private
start epoch and first-video PTS origin with the shared epoch and
`MediaInfo::start_time_ms`; retain the existing interruptible pacing and
video-only FFmpeg mode.

- [x] **Step 6: Verify and commit**

Run:

```bash
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev -R 'movie_audio_source|movie_video_source|ffmpeg_media_source|playback_session' --output-on-failure
git diff --check
```

Expected: real movie audio and video source tests pass.

Commit:

```bash
git add client/rtc client/media/playback tests/rtc
git commit -m "feat: pace isolated movie audio"
```

### Task 3: Add the independent movie-audio track

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Create: `client/rtc/webrtc/src/counting_audio_sink.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`

- [x] **Step 1: Write failing peer configuration tests**

Add tests proving:

```cpp
REQUIRE(!valid_signaled_peer_config({
    .role = SignaledRole::viewer,
    .movie_audio_source_factory = factory,
}));
REQUIRE(valid_signaled_peer_config({
    .role = SignaledRole::host,
    .movie_audio_source_factory = factory,
}));
```

Require a null-producing host factory to fail with
`movie-audio-source-unavailable`, while existing voice-only configurations
remain valid.

- [x] **Step 2: Run the peer test and verify RED**

Run:

```bash
cmake --build --preset build-call-dev
ctest --preset test-call-dev -R signaled_peer --output-on-failure
```

Expected: compilation fails because movie-audio configuration is missing.

- [x] **Step 3: Add configuration, track, sink, and result metrics**

Add:

```cpp
using LocalAudioSourceFactory =
    std::function<webrtc::scoped_refptr<LocalAudioSource>()>;

struct SignaledPeerResult {
  std::uint64_t movie_audio_frames_received{0};
  std::uint64_t movie_audio_invalid_frames_received{0};
  int movie_audio_sample_rate{0};
  int movie_audio_channels{0};
  int movie_audio_peak{0};
  std::uint64_t movie_audio_chunks_generated{0};
  std::optional<std::int64_t> movie_av_skew_ms;
};
```

Create `movie-audio` only for a host factory. Start and stop it independently
from the voice ADM track. In `OnTrack`, attach `CountingAudioSink` only when
`track->id() == "movie-audio"`; keep speaker playout disabled and keep voice
tracks separate.

- [x] **Step 4: Verify the optional dependency boundary**

Run:

```bash
cmake --build --preset build-call-dev
ctest --preset test-call-dev --output-on-failure
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev -R 'signaled_peer|movie_audio_source' --output-on-failure
```

Expected: call-only remains green without FFmpeg and combined tests pass.

- [x] **Step 5: Commit**

```bash
git add client/rtc/webrtc tests/rtc/signaled_peer_test.cpp
git commit -m "feat: add signaled movie audio track"
```

### Task 4: CLI, two-process acceptance, documentation, and integration

**Files:**
- Modify: `client/tools/signaled_call/main.cpp`
- Modify: `scripts/run_signaled_call_smoke.py`
- Modify: `README.md`
- Modify: `docs/architecture.md`
- Create: `docs/verification/signaled-movie-audio.md`
- Modify: `docs/superpowers/plans/2026-07-31-signaled-movie-audio.md`

- [x] **Step 1: Extend CLI and script contract tests**

Add `--movie-audio` as a host-only flag requiring movie video and a path.
Require the call-only binary to print only
`PEER_ERROR movie-audio-dependency-unavailable` and exit 1. Update the smoke
result parser for the new sanitized numeric fields and require the viewer
thresholds from the design.

- [x] **Step 2: Run validation and verify RED**

Run invalid combinations directly and run the movie smoke once:

```bash
python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server \
  --port 18117 \
  --audio microphone \
  --video movie \
  --movie-audio \
  --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
```

Expected: current parser rejects `--movie-audio`.

- [x] **Step 3: Wire shared factories and sanitized results**

When host movie audio is enabled:

```cpp
auto timeline = std::make_shared<shareme::rtc::MovieTimeline>();
config.video_source_factory = [movie_path, timeline](auto &) {
  return MovieVideoSource::create(movie_path, timeline);
};
config.movie_audio_source_factory = [movie_path, timeline] {
  return MovieAudioSource::create(movie_path, timeline);
};
```

Print only counters, dimensions, sample format, peak, A/V skew, candidate type,
and stable errors. Pass the movie path and movie-audio flag only to the host.

- [x] **Step 4: Run full feature-branch verification**

Run:

```bash
cd server && go test -count=1 -race ./... && go vet ./...
cd ..
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev --output-on-failure
cmake --build --preset build-call-dev
ctest --preset test-call-dev --output-on-failure
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev --output-on-failure
python3 scripts/run_signaled_call_smoke.py --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe --server-root server --port 18118 --audio synthetic --video synthetic
python3 scripts/run_signaled_call_smoke.py --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe --server-root server --port 18119 --audio microphone --video movie --movie-audio --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
git diff --check
```

Expected: all commands exit zero; movie smoke reports at least 100 stereo
48 kHz movie-audio callbacks, nonzero peak, at least 20 320x180 movie-video
frames, bidirectional voice RTP, and A/V skew at most 50 ms.

- [x] **Step 5: Record exact evidence and exclusions**

Document generated fixture properties, commands, counters, dependency versions,
review fixes, and platform limitations. Update the architecture and README
without claiming speaker playout, remote render-time synchronization, Windows,
TURN, or public-network verification.

- [ ] **Step 6: Commit, review, merge, reverify, and clean up**

```bash
git add client/tools scripts README.md docs
git commit -m "feat: verify signaled movie audio"
git push -u origin codex/stage6-movie-audio
```

Request independent review against the design and plan. Fix every
Critical/Important issue with TDD, rerun affected tests, then use the user's
standing authorization to merge with a non-fast-forward merge. Repeat Step 4
from `main`, push and verify `git ls-remote`, mark this step complete in a final
documentation commit, and remove the owned worktree and local feature branch.
