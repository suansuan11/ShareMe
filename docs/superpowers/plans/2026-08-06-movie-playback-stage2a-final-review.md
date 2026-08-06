# Movie Playback Stage 2A Final Review Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the final Stage 2A review findings without starting Stage 2B: prevent stale PCM after accepted generation/epoch changes, anchor from renderer logical consumption, wire remote pause/resume through audio and video playout, accept Qt `IdleState`, and correct the dynamic handoff.

**Architecture:** Keep `client/core` portable and retain the existing `AudioOutputDevice` lifecycle contract. On an accepted media-scope change, the renderer closes callback ingress, releases its stale owned blocks once, stops the current device, reopens it with the configured format, starts it, validates a fresh snapshot, and preserves `route_generation_`; failures mark local output and clock state invalid without affecting the peer. Remote playback state remains controller-owned: transitions call renderer pause/resume and the existing observational scheduler receives `playing`.

**Tech Stack:** C++20 portable core, Qt 6 multimedia adapter, Qt controller, CMake/CTest, Python static contract tests.

## Global Constraints

- Do not add `AudioOutputDevice::flush()` or route APIs.
- Preserve bounded callback ingress, exact teardown ordering, peer lifetime, and route-generation semantics.
- Do not enable video hold/drop/hard-resync correction or begin Stage 2B.
- Preserve the external libwebrtc cache at `/Users/dio/Library/Caches/ShareMe/webrtc`.
- Use TDD: each production behavior change follows a focused failing test.
- Append to `.superpowers/sdd/2026-08-05-movie-playback-three-stage/task-2A.6-report.md`; never overwrite prior evidence.

---

### Task 1: Add RED coverage for renderer scope invalidation and pause/resume

**Files:**
- Modify: `tests/core/movie_audio_renderer_test.cpp`
- Modify: `tests/rtc/video_preview_adapter_test.cpp`
- Modify: `tests/core/audio_output_contract_test.cpp`

**Interfaces:**
- The existing `AudioOutputDevice` interface remains unchanged.
- The renderer tests will exercise new public methods `pause_output()` and `resume_output()`.

- [ ] **Step 1: Extend the renderer fake with reopen and lifecycle telemetry**

Track `open_calls`, `start_calls`, `stop_calls`, `pause_calls`, `flush-equivalent stop/reopen writes`, and allow a reopened device to return a fresh valid snapshot with counters reset. Keep the fake's existing write capture so tests can distinguish old and new PCM bytes.

- [ ] **Step 2: Write the failing generation/epoch invalidation test**

Enqueue and pump an old block, leave a second old block queued, accept an anchor with a higher playback generation and host audio epoch, assert the old owned blocks are released exactly once, assert stop/open/start occur in that order, assert `route_generation` is unchanged, enqueue new PCM, pump, and assert only new bytes reach the reopened output. Assert a failed reopen leaves output inactive and clock confidence invalid while the renderer remains usable for later shutdown.

- [ ] **Step 3: Write the failing pause/resume test**

Activate an output, enqueue and pump PCM, call `pause_output()`, assert the output is paused and no more writes occur, call `resume_output()`, assert the same device is started without a route-generation increment, and assert queued PCM resumes exactly once.

- [ ] **Step 4: Run the focused RED build/test**

Run:

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_audio_renderer_test shareme_audio_output_contract_test
ctest --test-dir build/movie-call-dev --output-on-failure -R '^(movie_audio_renderer|audio_output_contract)$'
```

Expected result: compilation or assertions fail because the renderer APIs and scope restart behavior do not yet exist.

### Task 2: Add RED coverage for controller state, anchor origin, and Qt idle behavior

**Files:**
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `tests/scripts/CMakeLists.txt`

**Interfaces:**
- Pass the Qt audio adapter source path to the existing `rtc_demo_cli_contract` test.
- Keep the static contract test source-based; no new controller test binary or production route API is introduced.

- [ ] **Step 1: Add source arguments for the Qt adapter**

Pass `client/tools/rtc_demo/qt_audio_output_device.cpp` to the Python contract test and store it as `qt_audio_source`.

- [ ] **Step 2: Write failing static contract assertions**

Assert that the controller obtains `logical_consumed_frames` from the renderer snapshot for audio anchors, calls renderer pause/resume on remote state transitions, continues passing the remote state into `.playing`, and that the Qt adapter treats both `QAudio::ActiveState` and `QAudio::IdleState` as writable. Assert the renderer has no `AudioOutputDevice::flush` addition.

- [ ] **Step 3: Run the focused RED contract**

Run:

```bash
ctest --test-dir build/movie-call-dev --output-on-failure -R '^rtc_demo_cli_contract$'
```

Expected result: the new source assertions fail against the current controller and Qt adapter.

### Task 3: Implement renderer lifecycle behavior without changing the output contract

**Files:**
- Modify: `client/core/include/shareme/core/movie_audio_renderer.hpp`
- Modify: `client/core/src/movie_audio_renderer.cpp`
- Modify: `tests/core/movie_audio_renderer_test.cpp`
- Modify: `tests/rtc/video_preview_adapter_test.cpp`

**Interfaces:**
- Add `void pause_output() noexcept` and `void resume_output() noexcept` to `MovieAudioRenderer`.
- Keep `AudioOutputDevice` methods limited to `open`, `start`, `pause`, `stop`, `try_write`, `snapshot`, and `quiesce_and_snapshot`.

- [ ] **Step 1: Add the minimal renderer API and scope-change predicate**

Detect only strictly higher `playback_generation` or `audio_epoch` as a media-scope change. Reject regressions as today and do not restart for ordinary same-scope anchors.

- [ ] **Step 2: Implement bounded stale-block release**

Under `IngressBarrier`, stop admitting callbacks, release every non-free renderer slot exactly once, reset ready/in-flight/replay cursors, and preserve cumulative logical consumption and route generation. Do not replay stale accepted suffixes.

- [ ] **Step 3: Implement stop/reopen/start on the owning thread**

For an active output, invoke the existing device `stop()`, `open(config_.output_format)`, `start()`, and a fresh `snapshot()`. Validate the open result, start result, device identity, active state, sequence, and counter shape. Re-baseline device facts without incrementing `route_generation_`. If any operation fails, mark the output inactive, invalidate clock confidence through the existing discontinuity path, and leave the peer-independent renderer alive.

- [ ] **Step 4: Implement idempotent renderer pause/resume**

Pause through `output_->pause()` without releasing queued PCM or changing route generation. Resume through the existing `resume_old_output()` validation path, returning to active output only after a valid fresh snapshot. Treat failed resume as local output failure and clock invalidation.

- [ ] **Step 5: Run the focused GREEN tests**

Run:

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_audio_renderer_test
ctest --test-dir build/movie-call-dev --output-on-failure -R '^movie_audio_renderer$'
```

Expected result: the renderer invalidation, reopen failure, and pause/resume tests pass.

### Task 4: Implement controller wiring and Qt `IdleState` handling

**Files:**
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/qt_audio_output_device.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Controller calls `movie_audio_renderer_->pause_output()` and `resume_output()` only on accepted remote playback-state transitions.
- The scheduler continues receiving `.playing = remote_playback_state_ == QStringLiteral("playing")`.

- [ ] **Step 1: Use the renderer snapshot as the anchor origin**

Capture the current renderer snapshot immediately before constructing the accepted audio anchor and set `.consumed_frames` to `audio_snapshot.logical_consumed_frames`, while retaining the message generation, epoch, PTS, and format fields.

- [ ] **Step 2: Wire remote pause/resume transitions**

After the playback tracker accepts a state, compare it with the previous remote state. On a transition to `paused`, call `pause_output()`; on a transition to `playing`, call `resume_output()`. Keep state updates and scheduler input on the controller thread and leave peer lifecycle unchanged.

- [ ] **Step 3: Treat Qt idle output as writable**

Change sink-state classification so `QAudio::ActiveState` and `QAudio::IdleState` both retain `active_` when the I/O device exists. Keep suspended and error states non-writable and preserve existing trusted-fact invalidation.

- [ ] **Step 4: Run the focused GREEN contract**

Run:

```bash
cmake --build --preset build-movie-call-dev --target shareme_rtc_demo
ctest --test-dir build/movie-call-dev --output-on-failure -R '^rtc_demo_cli_contract$'
```

Expected result: all new controller/Qt source contracts pass.

### Task 5: Append evidence, update handoff, and verify the stage

**Files:**
- Modify: `.superpowers/sdd/2026-08-05-movie-playback-three-stage/task-2A.6-report.md`
- Modify: `docs/development/current-stage.md`

**Interfaces:**
- Preserve all prior report rounds and the `blocked-on-audio-correlation` and Stage 2B-not-started statements.
- Replace the stale Stage 2A checkpoint SHA with the final accepted commit SHA after Git verification.

- [ ] **Step 1: Run focused regression suites**

Run the renderer, scheduler, video adapter, movie peer, and CLI contract tests, including five repeated movie-peer runs.

- [ ] **Step 2: Run full macOS verification**

Run:

```bash
cmake --build --preset build-movie-call-dev --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure
git diff --check
```

Also run the required registered drift/performance contracts and the portable-core forbidden-header scan. Record macOS-only evidence and mark Windows/native media behavior environment-dependent.

- [ ] **Step 3: Append the final report round**

Record the RED commands, GREEN commands, stale PCM release/reopen behavior, pause/resume behavior, Qt idle contract, exact CTest result, cache preservation, and unchanged Stage 2B boundary.

- [ ] **Step 4: Inspect Git and commit only intended files**

Run `git status --short`, `git diff --check`, and `git diff --stat`; stage only the renderer, controller, Qt adapter, tests, report, handoff, and this related plan if it remains part of the final change. Commit with a focused message such as:

```bash
git add client/core/include/shareme/core/movie_audio_renderer.hpp client/core/src/movie_audio_renderer.cpp client/tools/rtc_demo/rtc_demo_controller.cpp client/tools/rtc_demo/qt_audio_output_device.cpp tests/core/movie_audio_renderer_test.cpp tests/core/audio_output_contract_test.cpp tests/rtc/video_preview_adapter_test.cpp tests/scripts/CMakeLists.txt tests/scripts/rtc_demo_cli_test.py .superpowers/sdd/2026-08-05-movie-playback-three-stage/task-2A.6-report.md docs/development/current-stage.md docs/superpowers/plans/2026-08-06-movie-playback-stage2a-final-review.md
git commit -m "fix: close final Stage 2A review gaps"
```

- [ ] **Step 5: Verify the final handoff SHA**

Run `git status --short --branch` and `git log --oneline -3`; confirm the handoff records the accepted final SHA, the worktree is clean, and no external cache or generated output is staged.
