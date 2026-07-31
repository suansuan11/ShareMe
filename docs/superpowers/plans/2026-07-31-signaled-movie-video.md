# Signaled Movie Video Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Send a host-only FFmpeg-decoded movie video track through the existing Qt/Go/libwebrtc signaled call and verify it in an independent viewer process.

**Architecture:** Generalize the existing test-pattern source behind a lifecycle and metrics contract, then add an optional movie source target that links the FFmpeg playback and WebRTC adapters. The host CLI injects the movie source through a factory; the viewer never opens the file. Existing synthetic video and synthetic/microphone audio paths remain unchanged.

**Tech Stack:** C++20, CMake/Ninja, FFmpeg, libyuv, native libwebrtc, Qt WebSockets, Go signaling, Python smoke orchestration.

---

### Task 1: Injectable local video source contract

**Files:**
- Create: `client/rtc/webrtc/include/shareme/rtc/local_video_source.hpp`
- Modify: `client/rtc/webrtc/src/test_pattern_source.hpp`
- Modify: `client/rtc/webrtc/src/test_pattern_source.cpp`
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `tests/rtc/test_pattern_source_test.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`

- [x] **Step 1: Write failing lifecycle/configuration tests**

Define the required contract in the tests before production code:

```cpp
class LocalVideoSource : public webrtc::AdaptedVideoTrackSource {
public:
  virtual bool start() = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual std::uint64_t generated_count() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t dropped_count() const noexcept = 0;
  [[nodiscard]] virtual std::string error() const = 0;
};
```

Assert that `TestPatternSource` is usable through `LocalVideoSource`, starts
exactly once, stops idempotently, and has no error. Add
`SignaledVideoMode { synthetic, injected }`,
`LocalVideoSourceFactory`, and tests proving injected mode without a factory is
invalid while a non-null test factory is valid.

- [x] **Step 2: Run focused tests and confirm RED**

```bash
cmake --build --preset build-call-dev
```

Expected: compilation fails because `LocalVideoSource`,
`SignaledVideoMode`, and `video_source_factory` do not exist.

- [x] **Step 3: Implement the minimum source contract and routing**

Make `TestPatternSource` implement `LocalVideoSource`. In
`SignaledPeer::initialize`, use the factory only for injected mode:

```cpp
if (config_.video_mode == SignaledVideoMode::injected) {
  video_source_ = config_.video_source_factory(*queues_);
} else {
  video_source_ = TestPatternSource::create(*queues_, 640, 360, 30);
}
if (!video_source_) {
  fail("video-source-unavailable");
  return false;
}
```

In `start()`, fail with `video_source_->error()` or
`video-source-start-failed` instead of falling back. During `wait()`, surface
an asynchronous source error before collecting stats.

- [x] **Step 4: Verify and commit**

```bash
cmake --build --preset build-call-dev
ctest --preset test-call-dev -R 'test_pattern_source|signaled_peer' --output-on-failure
git add client/rtc/webrtc tests/rtc/test_pattern_source_test.cpp tests/rtc/signaled_peer_test.cpp
git commit -m "feat: inject signaled local video sources"
```

Expected: both focused tests pass.

### Task 2: FFmpeg-backed movie video source

**Files:**
- Create: `client/rtc/movie/CMakeLists.txt`
- Create: `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp`
- Create: `client/rtc/movie/src/movie_video_source.cpp`
- Modify: `client/rtc/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`
- Create: `tests/rtc/movie_video_source_test.cpp`
- Modify: `CMakePresets.json`

- [x] **Step 1: Add a combined preset and failing movie-source test**

Add `movie-call-dev`, `build-movie-call-dev`, and `test-movie-call-dev`. The
configure preset inherits `base` and enables all three optional dependencies:

```json
{
  "name": "movie-call-dev",
  "inherits": "base",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Debug",
    "SHAREME_ENABLE_QT": "ON",
    "SHAREME_ENABLE_FFMPEG": "ON",
    "SHAREME_ENABLE_WEBRTC": "ON"
  }
}
```

Register a two-second 320x180 30 fps fixture using the installed `ffmpeg`
executable. The C++ test creates `MovieVideoSource`, attaches
`CountingVideoSink`, starts it, waits for at least 20 frames, and asserts:

```cpp
REQUIRE(sink.last_width() == 320);
REQUIRE(sink.last_height() == 180);
REQUIRE(sink.timestamps_increase());
REQUIRE(sink.last_luma_min() < sink.last_luma_max());
REQUIRE(source->generated_count() >= 20);
REQUIRE(source->error().empty());
```

Also test that a nonexistent path produces `movie-open-failed` without any
generated frames.

- [x] **Step 2: Configure/build and confirm RED**

```bash
cmake --fresh --preset movie-call-dev \
  -DWEBRTC_ROOT=/Users/dio/Library/Caches/ShareMe/webrtc \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-movie-call-dev
```

Expected: compilation/configuration fails because the movie adapter target and
class are not implemented.

- [x] **Step 3: Implement decoding, conversion, pacing, and shutdown**

`MovieVideoSource::start()` opens `FfmpegMediaSource` through
`PlaybackSession`, verifies `has_video`, starts playback, and starts one
`std::jthread`. The worker advances the session playhead from
`std::chrono::steady_clock`, pops decoded frames, converts FFmpeg RGBA memory
using `libyuv::ABGRToI420`, calls `AdaptFrame`, and emits:

```cpp
auto frame = webrtc::VideoFrame::Builder()
    .set_video_frame_buffer(i420)
    .set_timestamp_us(webrtc::TimeMicros())
    .set_rtp_timestamp(static_cast<std::uint32_t>(pts_ms * 90))
    .set_rotation(webrtc::kVideoRotation_0)
    .build();
OnFrame(frame);
```

Guard error text and counters with atomics/mutexes. `stop()` requests/join the
worker before `PlaybackSession::close()` and is idempotent.

- [x] **Step 4: Verify source behavior and optional boundaries**

```bash
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev -R 'movie_video_source|ffmpeg_media_source|playback_session' --output-on-failure
cmake --build --preset build-call-dev
ctest --preset test-call-dev -R 'test_pattern_source|signaled_peer' --output-on-failure
```

Expected: movie tests pass in the combined build and call-only tests still pass
without FFmpeg.

- [x] **Step 5: Commit**

```bash
git add CMakePresets.json client/rtc/CMakeLists.txt client/rtc/movie tests/rtc
git commit -m "feat: decode movies into WebRTC video frames"
```

### Task 3: CLI integration and two-process movie verification

**Files:**
- Modify: `client/tools/signaled_call/CMakeLists.txt`
- Modify: `client/tools/signaled_call/main.cpp`
- Modify: `scripts/run_signaled_call_smoke.py`

- [x] **Step 1: Add failing CLI/script acceptance**

Extend the probe contract with `--video synthetic|movie` and `--movie path`.
Reject movie mode for a viewer, a missing `--movie`, or `--movie` without movie
mode with exit code 2. Extend sanitized results:

```text
RESULT connected=1 video=60 width=320 height=180 audio_sent=101 audio_received=101 audio_level=0.01 candidate=host error=
```

Add `--video` and `--movie` to the Python script. Only the host receives the
movie arguments. In movie mode require the viewer result to contain at least 20
frames at 320x180 while retaining nonzero voice RTP requirements for both
peers.

- [x] **Step 2: Confirm the new smoke command fails before wiring**

```bash
python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server --port 18100 --audio microphone \
  --video movie --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
```

Expected: argument or result validation failure.

- [x] **Step 3: Wire the host movie factory**

When both `ShareMe::Playback` and the movie source target exist, link the
signaled call probe to it and define `SHAREME_HAS_MOVIE_VIDEO=1`. In movie mode
build the factory without printing the path:

```cpp
config.video_mode = SignaledVideoMode::injected;
config.video_source_factory =
    [movie_path](webrtc::TaskQueueFactory &) {
      return MovieVideoSource::create(movie_path);
    };
```

Populate result width/height from `CountingVideoSink`. Keep the existing
synthetic default and return a typed dependency error if a movie-enabled CLI is
not built.

- [x] **Step 4: Run synthetic and real movie calls**

```bash
python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server --port 18101 --audio synthetic --video synthetic
python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server --port 18102 --audio microphone \
  --video movie --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
```

Expected: both calls exit zero; the movie viewer reports at least 20 frames at
320x180 and both microphone directions have positive packet counts and levels.

- [x] **Step 5: Commit**

```bash
git add client/tools/signaled_call scripts/run_signaled_call_smoke.py
git commit -m "feat: verify signaled movie video"
```

### Task 4: Documentation, full verification, and integration

**Files:**
- Modify: `README.md`
- Modify: `docs/architecture.md`
- Create: `docs/verification/signaled-movie-video.md`
- Modify: `docs/superpowers/plans/2026-07-31-signaled-movie-video.md`

- [ ] **Step 1: Record exact evidence and exclusions**

Document the build commands, generated fixture, viewer frame count and
dimensions, microphone packet counters, permission behavior, locked dependency
path, and exact platform. State explicitly that movie audio, production UI,
hardware H.264, Windows native media, TURN, and public-network behavior remain
unverified.

- [ ] **Step 2: Run the complete feature-branch matrix**

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
python3 scripts/run_signaled_call_smoke.py --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe --server-root server --port 18103 --audio synthetic --video synthetic
python3 scripts/run_signaled_call_smoke.py --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe --server-root server --port 18104 --audio microphone --video movie --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
git diff --check
```

Expected: every command exits zero and both smoke runs print only sanitized
results.

- [ ] **Step 3: Commit, review, merge, and reverify main**

```bash
git add README.md docs
git commit -m "docs: record signaled movie video verification"
git push -u origin codex/stage5-movie-video
```

Request an independent review of the branch range against the design and this
plan. Fix every Critical/Important issue and rerun affected tests. With the
user's standing authorization, merge using a non-fast-forward merge, repeat
Step 2 from `main`, push `main`, verify `git ls-remote`, then remove the owned
`.worktrees/stage5-movie-video` worktree and local feature branch.
