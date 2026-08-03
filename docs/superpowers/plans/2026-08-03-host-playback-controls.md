# Host Playback Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add host-authoritative pause, resume, and seek to the shared movie timeline, both movie sources, the RTC controller, and sender UI while publishing the correct absolute PTS and generation.

**Architecture:** `MovieTimeline` is the sole synchronized movie clock. Independent video and movie-audio sources seek their own `PlaybackSession` when its generation changes and block on the same stop-aware timeline; Qt only calls the timeline and publishes snapshots.

**Tech Stack:** C++20, Qt 6/QML, FFmpeg, libwebrtc, CMake/CTest, Go signaling.

## Global Constraints

- Keep `client/core` portable and unchanged; timeline/media code remains outside it.
- Movie audio, host voice, and viewer voice retain independent lifecycle and verification.
- Keep existing queue capacities and overflow policies unchanged.
- Use absolute media PTS in the control protocol; seek increments generation exactly once.
- Do not implement viewer controls, hard resync, speaker playout, TURN, or Windows-only claims.
- Preserve the repository-external libwebrtc cache and exclude generated output.

---

### Task 1: Shared controllable movie timeline

**Files:**
- Modify: `client/rtc/movie/include/shareme/rtc/movie_timeline.hpp`
- Modify: `client/rtc/movie/src/movie_timeline.cpp`
- Create: `tests/rtc/movie_timeline_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `MovieTimelineState`, `MovieTimelineSnapshot`, `MovieTimelineWaitResult`.
- Produces: `initialize(start_pts_ms, duration_ms)`, `std::optional<MovieTimelineSnapshot> snapshot()`, `pause()`, `resume()`, `seek(target_pts_ms)`, and `wait_until(target_pts_ms, generation, stop_token)`.

- [x] **Step 1: Write failing timeline state tests**

Add a dedicated test executable whose assertions require this public shape:

```cpp
auto timeline = std::make_shared<shareme::rtc::MovieTimeline>();
REQUIRE(timeline->initialize(5'000, 2'000));
const auto initial = timeline->snapshot();
REQUIRE(initial.has_value());
REQUIRE(initial->state == shareme::rtc::MovieTimelineState::playing);
REQUIRE(initial->start_pts_ms == 5'000);
REQUIRE(initial->duration_ms == 2'000);
REQUIRE(initial->generation == 0);
REQUIRE(timeline->pause());
const auto paused = *timeline->snapshot();
std::this_thread::sleep_for(30ms);
REQUIRE(timeline->snapshot()->media_pts_ms == paused.media_pts_ms);
REQUIRE(timeline->resume());
REQUIRE(timeline->seek(6'500));
REQUIRE(timeline->snapshot()->media_pts_ms >= 6'500);
REQUIRE(timeline->snapshot()->generation == 1);
REQUIRE(!timeline->seek(4'999));
REQUIRE(!timeline->seek(7'001));
```

Also assert duplicate initialization matches, mismatched start/duration fails,
`INT64_MAX` range overflow fails, pause/resume do not increment generation,
seek while paused preserves pause, and a `std::jthread` waiting on a future PTS
returns `stopped` promptly when its stop token is requested.

- [x] **Step 2: Run the new test and verify RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_timeline_test
```

Expected: compilation fails because the new timeline types and control methods do not exist.

- [x] **Step 3: Implement the timeline state machine**

Define the public data contract:

```cpp
enum class MovieTimelineState { playing, paused };
enum class MovieTimelineWaitResult { due, generation_changed, stopped };
struct MovieTimelineSnapshot {
  MovieTimelineState state;
  std::int64_t start_pts_ms;
  std::int64_t duration_ms;
  std::int64_t media_pts_ms;
  std::uint64_t generation;
  std::uint64_t revision;
};
```

Use one mutex plus `condition_variable_any`. Compute the end PTS with checked
addition. Every mutating method wakes waiters. `wait_until` loops after
pause/resume revisions, returns `generation_changed` on a seek, and uses the
caller stop token for shutdown.

- [x] **Step 4: Run timeline test GREEN and refactor**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_timeline_test
ctest --preset test-movie-call-dev -R '^movie_timeline$' --output-on-failure
```

- [x] **Step 5: Commit timeline core**

```bash
git add client/rtc/movie/include/shareme/rtc/movie_timeline.hpp client/rtc/movie/src/movie_timeline.cpp tests/rtc/movie_timeline_test.cpp tests/rtc/CMakeLists.txt
git commit -m "feat: add controllable movie timeline"
```

### Task 2: Generation-aware movie video

**Files:**
- Modify: `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp`
- Modify: `client/rtc/movie/src/movie_video_source.cpp`
- Modify: `tests/rtc/movie_video_source_test.cpp`

**Interfaces:**
- Consumes: Task 1 timeline snapshot and wait APIs.
- Produces: video delivery that freezes during pause and seeks on generation change.

- [x] **Step 1: Write failing pause/resume/seek integration tests**

Use the generated nonzero-PTS movie. Start a source and sink, pause its shared
timeline after at least five frames, and assert the count grows by at most one
in 150 ms. Resume and assert growth. Seek to `start + 1'000`, then assert the
last emitted PTS reaches the target and no pre-seek frame is delivered after
the first post-seek frame.

```cpp
REQUIRE(timeline->pause());
const auto paused_count = sink.frame_count();
std::this_thread::sleep_for(150ms);
REQUIRE(sink.frame_count() <= paused_count + 1);
REQUIRE(timeline->resume());
REQUIRE(timeline->seek(6'000));
```

- [x] **Step 2: Run video test and verify RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_video_source_test
ctest --preset test-movie-call-dev -R '^movie_video_source$' --output-on-failure
```

Expected: new assertions fail because the source still owns a fixed epoch.

- [x] **Step 3: Make video obey timeline state and generation**

Initialize the timeline from `MediaInfo`; fail with
`movie-timeline-mismatch` if it rejects the values. Apply a new generation with
`PlaybackSession::seek()`. Pause the session when the timeline is paused. Before
`OnFrame`, require `wait_until(frame.pts_ms, generation, stop_token) == due`;
discard and restart on `generation_changed`.

- [x] **Step 4: Run video tests GREEN**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_video_source_test
ctest --preset test-movie-call-dev -R '^(movie_timeline|movie_video_source)$' --output-on-failure
```

- [x] **Step 5: Commit generation-aware video**

```bash
git add client/rtc/movie/include/shareme/rtc/movie_video_source.hpp client/rtc/movie/src/movie_video_source.cpp tests/rtc/movie_video_source_test.cpp
git commit -m "feat: control movie video playback"
```

### Task 3: Generation-aware independent movie audio

**Files:**
- Modify: `client/rtc/movie/include/shareme/rtc/movie_audio_source.hpp`
- Modify: `client/rtc/movie/src/movie_audio_source.cpp`
- Modify: `tests/rtc/movie_audio_source_test.cpp`

**Interfaces:**
- Consumes: Task 1 timeline state/generation and Task 2 semantics.
- Produces: audio callbacks that pause and seek without affecting voice paths.

- [x] **Step 1: Write failing audio pause/resume/seek tests**

With `CountingPcmSink`, pause after at least ten callbacks and allow at most one
in-flight callback in 150 ms. Resume and require growth. Seek forward and
assert `last_pts_ms()` reaches the requested target without a lower subsequent
PTS. Repeat the shared audio/video offset test after one coordinated seek.

- [x] **Step 2: Run audio test and verify RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_audio_source_test
ctest --preset test-movie-call-dev -R '^movie_audio_source$' --output-on-failure
```

Expected: new assertions fail because audio still paces from a fixed epoch.

- [x] **Step 3: Make audio obey the shared generation**

Mirror Task 2. On generation change call
`PlaybackSession::seek(snapshot.media_pts_ms)` and replace `chunker_` with a new
`PcmChunker` before decoding more input. Never pause or restart the native voice
audio device or voice tracks.

- [x] **Step 4: Run all movie-source tests GREEN**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_timeline_test shareme_movie_video_source_test shareme_movie_audio_source_test
ctest --preset test-movie-call-dev -R '^(movie_timeline|movie_video_source|movie_audio_source)$' --output-on-failure
```

- [x] **Step 5: Commit synchronized audio controls**

```bash
git add client/rtc/movie/include/shareme/rtc/movie_audio_source.hpp client/rtc/movie/src/movie_audio_source.cpp tests/rtc/movie_audio_source_test.cpp
git commit -m "feat: control movie audio playback"
```

### Task 4: Host controller, state generation, and sender UI

**Files:**
- Modify: `client/tools/rtc_demo/playback_state.hpp`
- Modify: `client/tools/rtc_demo/playback_state.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `tests/rtc/playback_state_test.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Consumes: timeline snapshot/control methods from Task 1.
- Produces: QML properties `hostPlaybackState`, `hostPlaybackPositionMs`, `hostPlaybackStartMs`, `hostPlaybackDurationMs`, `hostPlaybackGeneration`, `hostControlsAvailable`.
- Produces: invokables `pauseHostPlayback()`, `resumeHostPlayback()`, `seekHostPlayback(qint64 absolute_pts_ms)`.

- [x] **Step 1: Write failing playback-state generation tests**

Change `make_movie_playback_state` to accept a transport-layer movie state and
generation. The small `MoviePlaybackState` enum keeps this protocol helper
buildable when optional FFmpeg/movie targets are disabled:

```cpp
const auto paused = make_movie_playback_state(
    QStringLiteral("ABC234"), 10, 6'000, 92'000,
    shareme::tools::MoviePlaybackState::paused, 3);
REQUIRE(paused->state == QStringLiteral("paused"));
REQUIRE(paused->media_pts_ms == 6'000);
REQUIRE(paused->generation == 3);
```

- [x] **Step 2: Run playback-state test and verify RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_playback_state_test
ctest --preset test-movie-call-dev -R '^playback_state$' --output-on-failure
```

Expected: compilation fails because the state factory lacks timeline state and generation.

- [x] **Step 3: Implement controller properties and commands**

Sample `movie_timeline_->snapshot()` in the timer. Publish its state, PTS, and
generation; retain EOF final-paused behavior. Each accepted host command calls
one timeline method, emits host notification, publishes immediately, and
leaves the periodic timer active. Reject viewer, non-movie, not-started, ended,
and out-of-range calls.

- [x] **Step 4: Add bounded sender controls in QML**

Show Pause/Resume and a duration-bounded slider only for a controllable movie
host. Convert normalized slider milliseconds back to absolute PTS on user
commit; property refresh must not trigger a seek.

- [x] **Step 5: Build Qt demo and run focused contracts GREEN**

```bash
cmake --build --preset build-movie-call-dev --target shareme_rtc_demo shareme_playback_state_test
ctest --preset test-movie-call-dev -R '^(playback_state|rtc_demo_cli_contract)$' --output-on-failure
```

- [x] **Step 6: Commit host control integration**

```bash
git add client/tools/rtc_demo/playback_state.* client/tools/rtc_demo/rtc_demo_controller.* client/tools/rtc_demo/qml/Main.qml tests/rtc/playback_state_test.cpp tests/scripts/rtc_demo_cli_test.py
git commit -m "feat: add host movie playback controls"
```

### Task 5: Stage verification and handoff

**Files:**
- Create: `docs/verification/host-playback-controls.md`
- Modify: `docs/development/current-stage.md`
- Modify: `README.md`
- Modify: `docs/superpowers/plans/2026-08-03-host-playback-controls.md`

**Interfaces:**
- Consumes: final code/tests from Tasks 1-4.
- Produces: exact platform evidence, limitations, commits, and next-stage handoff.

- [x] **Step 1: Run complete macOS verification**

```bash
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev --output-on-failure
git diff --check
(cd server && go test -count=1 -race ./... && go vet ./...)
python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py
python3 scripts/validate_shareme_skill.py
```

Record exact counts. Do not convert macOS evidence into Windows verification.

- [x] **Step 2: Review concurrency, lifetime, and Git scope**

Inspect timeline lock ordering, stop interruption, generation discard, audio
chunk reset, Qt ownership, protocol sequence, and unrelated/generated files.
Repair every Critical or Important finding and repeat affected tests.

- [x] **Step 3: Update stage documents**

Record verified host controls, partial GUI evidence, environment-dependent
Windows reruns, and unimplemented hard resync/speaker playout. Correct the
deterministic-workflow handoff to merge `834c917`.

- [x] **Step 4: Commit stage evidence**

```bash
git add README.md docs/development/current-stage.md docs/verification/host-playback-controls.md docs/superpowers/plans/2026-08-03-host-playback-controls.md
git commit -m "docs: record host playback control verification"
```

- [x] **Step 5: Integrate under existing user authority**

Verify a clean feature worktree, push the feature branch, merge into a clean
`main`, rerun final gates on merged `main`, push `main`, verify both remote refs,
and remove only the completed local worktree/branch. Preserve the external
libwebrtc cache and remote feature branch.
