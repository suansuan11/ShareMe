# Task 2A.6 Report: Stage 2A Integration, Checkpoint, And Commit

Status: DONE_WITH_CONCERNS
Platform: macOS Darwin arm64

## Outcome

Stage 2A integration is committed at `750f7c9` (`feat: integrate Stage 2A
movie playback`). The change is limited to lifecycle orchestration and
verification. `MovieAudioPeer` remains dedicated transport, the viewer uses
`native_playout=false`, voice remains on the primary peer/ADM path, and
production video correction remains observational/pass-through.

The approved teardown sequence is implemented in
`RtcDemoController::stopPeer()`:

1. mark the controller shutting down;
2. close renderer and scheduler ingress;
3. stop the renderer pump, anchor, report, drift, and performance timers;
4. quiesce the renderer output on the controller Qt thread;
5. stop the output and release PCM blocks, then release video tokens exactly
   once;
6. stop `MovieAudioPeer` after its waiter is joined;
7. stop the primary peer after its waiter is joined; and
8. destroy the renderer and video adapter last.

The PCM callback captures only the renderer pointer and calls `try_enqueue`.
The renderer's closed-ingress admission check runs before locks or PCM copy.
The Qt output adapter is constructed, activated, pumped, quiesced, and stopped
only from controller-thread code.

## Files Changed

The implementation commit includes all preserved Task 2A.4 and Task 2A.5
changes plus the Task 2A.6 integration:

- Core: `client/core/CMakeLists.txt`, `movie_audio_renderer.hpp/.cpp`, and
  `movie_video_scheduler.hpp/.cpp`.
- Transport/protocol: `local_audio_source.hpp`, `movie_audio_source.hpp/.cpp`,
  `movie_audio_peer.hpp/.cpp`, and the movie audio clock message files.
- Qt demo: `movie_video_playout_adapter.hpp/.cpp`, `rtc_demo_controller.hpp/.cpp`,
  `playout_report.hpp/.cpp`, `qt_audio_output_device.cpp`, QML, and CMake.
- Tests: core renderer/scheduler registration and tests; movie source/peer and
  clock message tests; playout report and video adapter tests; RTC CMake; and
  the Python CLI contract.
- Handoff: `docs/development/current-stage.md` and this report.

## TDD Evidence

### RED

The first failing contract run was:

```text
ctest --test-dir build/movie-call-dev --output-on-failure -R '^rtc_demo_cli_contract$'
```

Observed output:

```text
Ran 20 tests in 0.952s
FAILED (failures=1, errors=1)
FAIL: test_controller_uses_app_owned_movie_renderer_and_preserves_voice_path
ERROR: test_controller_teardown_is_dependency_safe
0% tests passed, 1 tests failed out of 1
```

The failures were the expected missing renderer contract and missing teardown
markers. The focused C++ RED build was:

```text
cmake --build --preset build-movie-call-dev --target \
  shareme_movie_audio_renderer_test shareme_video_preview_adapter_test
```

It failed only because the new tests referenced the not-yet-implemented
`MovieAudioRenderer::close_ingress`,
`MovieVideoPlayoutAdapter::close_ingress`, and
`MovieVideoPlayoutAdapter::shutdown` APIs.

### GREEN

After the minimum implementation:

```text
cmake --build --preset build-movie-call-dev --target \
  shareme_movie_audio_renderer_test shareme_video_preview_adapter_test shareme_rtc_demo
```

completed through the demo link step. Focused CTest output:

```text
ctest --test-dir build/movie-call-dev --output-on-failure \
  -R '^(movie_audio_renderer|movie_video_scheduler|movie_audio_peer|movie_audio_clock_message|playout_report|video_preview_adapter)$'
100% tests passed, 0 tests failed out of 6
```

The replacement registered contract passed independently:

```text
ctest --test-dir build/movie-call-dev --output-on-failure \
  -R '^rtc_demo_cli_contract$'
100% tests passed, 0 tests failed out of 1
```

## Verification

- Full configured build: `cmake --build --preset build-movie-call-dev --parallel 4`, completed through `[51/51]`.
- Full configured CTest: `ctest --test-dir build/movie-call-dev --output-on-failure`, passed `57/57` in `28.16 sec`.
- Required registered contracts: `rtc_demo_cli_contract`,
  `movie_drift_study_contract`, and `movie_performance_study_contract` passed
  `3/3` in `1.02 sec`.
- Focused stall coverage is in `tests/rtc/video_preview_adapter_test.cpp` for
  deterministic `500 ms` and `2 s` stalls. It asserts at most three held
  tokens, candidate observation telemetry, clock-blocked telemetry, renderer
  audio queue continuity, and no applied action in observational mode.
- `git diff --check`: passed with no output.
- Portable scan: the `client/core` C++ include scan for Qt, FFmpeg, WebRTC,
  GPU, and OS headers reported `portable client/core forbidden-header scan: no
  matches`.
- The external WebRTC cache at `/Users/dio/Library/Caches/ShareMe/webrtc` was
  used read-only and was not modified, rebuilt, or staged.

## Correlation Feasibility

Result: `blocked-on-audio-correlation`.

The locked WebRTC `RemoteAudioSource` path does not expose the sender media
timestamp to the application callback. `MovieAudioPeerCallbacks::pcm` exposes
only a synchronous PCM view and a receiver-local sequence. The host's
`LocalAudioSourceClockSnapshot` contains a host source sequence, but there is
no shared source/decoded sequence or approved mapping between that value and
the viewer callback sequence. The optional callback timestamp is not a sender
media timestamp and is not used as a correlation value.

The implementation therefore accepts anchors for observation, keeps renderer
confidence provisional, reports `clock-blocked` when appropriate, and never
enables production hold/drop/hard-resync correction. Stage 2B measurement and
correction tasks 2B.2 and 2B.3 were not started. The `750f7c9` implementation
commit is the rollback point for any future experiment.

## Concerns

- Verification is macOS-only. Windows native media, route changes, and native
  device behavior remain environment-dependent and are not claimed here.
- The Qt offscreen/device-contract tests pass, but this task does not claim a
  live two-process native output or physical acoustic/display synchronization
  result.
- Stage 2B remains intentionally blocked until a shared correlation value or
  separately approved estimator exists. No drift or hard-resync measurement was
  started from this branch.

## Review-Fix Round 1

Status: VERIFIED_WITH_CONCERNS
Platform: macOS Darwin arm64

The first review-fix round addressed all five findings without changing the
Stage 2A scope or enabling Stage 2B correction:

- `MovieAudioCallbackSink` now admits PCM callbacks through an in-flight count
  and `close_and_wait()` barrier. `MovieAudioPeer::stop()` closes and waits for
  the sink before removing the WebRTC sink or allowing the peer implementation
  to be destroyed. The direct sink test blocks an active callback and proves
  that close cannot complete until the callback leaves.
- `MovieVideoPlayoutSchedulerConfig::observational()` names the production
  default. The controller passes that configuration explicitly; pure policy
  tests continue to use `apply_policy=true` and production remains
  pass-through.
- `MovieVideoPlayoutAdapter::submit()` uses `std::try_to_lock` and returns a
  pass-through result when the adapter mutex is busy. A blocking `ToI420()` test
  proves a concurrent submit does not wait behind conversion.
- The stall-test audio output now consumes 480 frames per snapshot, enforces a
  960-frame device queue, and returns `would_block` at capacity. The 500 ms and
  2 s tests assert consumption, logical progress, bounded device queue, and
  backpressure without discontinuity.
- Controller startup now checks `activate_output()`, reports
  `movie-audio-output-activation-failed`, avoids starting the audio pump and
  movie peer when output activation fails, and preserves the failure status.

### Review-Fix TDD Evidence

The RED checks were observed before the implementation:

- the scheduler target failed because the named observational configuration did
  not exist;
- the video adapter test failed at the concurrent-submit completion assertion;
- the CLI contract failed because the controller did not name observational
  mode or inspect activation status; and
- the direct callback-sink test failed to compile because the explicit barrier
  did not exist.

After the implementation, the focused command passed all four targets:

```text
ctest --test-dir build/movie-call-dev --output-on-failure \
  -R '^(movie_audio_peer|video_preview_adapter|movie_video_scheduler|rtc_demo_cli_contract)$'
100% tests passed, 0 tests failed out of 4
```

### Review-Fix Verification

- Full configured build: `cmake --build --preset build-movie-call-dev --parallel 4`,
  completed through `[35/35]`.
- Full configured CTest:
  `ctest --test-dir build/movie-call-dev --output-on-failure`, passed `57/57`
  in `30.20 sec`.
- Required registered contracts passed `3/3`:
  `rtc_demo_cli_contract`, `movie_drift_study_contract`, and
  `movie_performance_study_contract`.
- `git diff --check`: passed with no output.
- Portable scan: `client/core/include` and `client/core/src` reported
  `portable client/core forbidden-header scan: no matches`.
- The repository-external WebRTC cache at
  `/Users/dio/Library/Caches/ShareMe/webrtc` was preserved and used read-only.

Windows native media behavior, live native output, and physical acoustic or
display synchronization remain environment-dependent and are not claimed by
this macOS review-fix verification. Stage 2B correction remains unimplemented
and disabled.

## Review-Fix Round 2

Status: VERIFIED_WITH_CONCERNS
Platform: macOS Darwin arm64

Round 2 addressed the two remaining review findings without altering the prior
commits, teardown order, observational video mode, or Stage 2B boundary:

- `MovieAudioCallbackSink` now uses a packed atomic admission state with a
  closed-ingress bit and in-flight count. `OnData()` performs the atomic
  admission operation before taking `callback_mutex_` or inspecting PCM. A
  callback that wins admission is counted before callback copying and remains
  covered by `close_and_wait()`; late callbacks return without mutex
  contention. The deterministic test blocks an admitted callback while its
  callback object is copied, closes ingress, and proves a late `OnData()`
  returns before that mutex is released.
- `RtcDemoController` persists `movie_audio_output_ready_` for the call. The
  primary-peer waiter reports `connected` only when its own wait succeeds and
  local movie output is ready; it does not overwrite
  `movie-audio-output-activation-failed`. Primary peer failure handling remains
  active, so local output failure is not fatal to voice/video/control.

### Review-Fix TDD Evidence

The RED checks were observed before the production changes:

- the callback test failed to compile because the explicit close-ingress seam
  did not exist; and
- the controller contract failed because no persistent output-ready member
  existed, and the waiter had no guarded success branch.

The affected tests then passed:

```text
ctest --test-dir build/movie-call-dev --output-on-failure \
  --repeat until-fail:5 -R '^movie_audio_peer$'
100% tests passed, 0 tests failed out of 1

ctest --test-dir build/movie-call-dev --output-on-failure \
  -R '^rtc_demo_cli_contract$'
100% tests passed, 0 tests failed out of 1
```

### Review-Fix Verification

- Full configured build: `cmake --build --preset build-movie-call-dev --parallel 4`.
- Full configured CTest: `ctest --test-dir build/movie-call-dev
  --output-on-failure`, passed `57/57` in `28.87 sec`.
- The external WebRTC cache remained preserved and was not cleaned, rebuilt, or
  staged.
- No correction, route work, estimator, Stage 2B measurement, or production
  hold/drop/hard-resync wiring was added.

Windows native media behavior and live native output remain
environment-dependent and are not claimed by this macOS verification.

## Review-Fix Round 3

Status: VERIFIED_WITH_CONCERNS
Platform: macOS Darwin arm64
Implementation commit: `34fc3b9` (`fix: close final Stage 2A review gaps`)

This final allowed review-fix round addresses the remaining three Important
findings and one Minor handoff finding without starting Stage 2B or changing
the existing route/controller APIs:

- An accepted increase in playback generation or host audio epoch now closes
  renderer callback ingress, releases all stale renderer-owned PCM blocks once,
  stops the current output, reopens it with the configured format, starts it,
  validates a fresh zero-queue snapshot, and re-baselines device facts. The
  existing `route_generation` remains unchanged. Reopen, start, or snapshot
  failure stops the local output, records `audio_output_failure`, and
  invalidates the renderer clock while leaving the peer alive. A paused output
  follows the same stop/reopen/start path and is paused again before new PCM
  can play.
- The viewer's audio anchor now uses the current renderer
  `logical_consumed_frames` rather than a constant zero origin.
- Accepted remote playback-state transitions call renderer pause/resume on the
  controller thread. The existing observational video scheduler continues to
  receive `playing=false` while paused, and queued audio survives an ordinary
  pause/resume without a route-generation change.
- Qt sink classification treats both `QAudio::ActiveState` and
  `QAudio::IdleState` as writable when an I/O device exists. Suspended and error
  states remain non-writable.
- The dynamic handoff now records `34fc3b9`; audio correlation remains
  `blocked-on-audio-correlation`, and Stage 2B tasks 2B.2 and 2B.3 remain
  unstarted.

### Review-Fix TDD Evidence

The RED checks were observed before the production implementation:

- the renderer target failed to compile because `pause_output()` and
  `resume_output()` did not yet exist;
- the CLI contract failed in
  `test_final_movie_audio_review_contracts` because the controller still used
  a zero anchor origin and lacked the new lifecycle/static contracts.

The renderer RED coverage then drove deterministic tests for stale-block
release, stop/open/start ordering, fresh-output validation, reopen failure,
route-generation preservation, ordinary pause/resume, and scope change while
paused.

### Review-Fix Verification

- Focused regression passed `5/5` for `movie_audio_renderer`,
  `movie_video_scheduler`, `movie_audio_peer`, `video_preview_adapter`, and
  `rtc_demo_cli_contract`.
- Five consecutive `movie_audio_peer` runs passed.
- Full configured build passed with
  `cmake --build --preset build-movie-call-dev --parallel 4`.
- Full configured CTest passed `57/57` on macOS arm64.
- Required registered contracts passed `3/3`:
  `rtc_demo_cli_contract`, `movie_drift_study_contract`, and
  `movie_performance_study_contract`.
- `git diff --check` passed with no output.
- The portable `client/core` forbidden-header scan reported no matches for Qt,
  FFmpeg/libav, WebRTC, Windows, Direct3D, WASAPI, CoreAudio, or other OS
  headers.
- The repository-external WebRTC cache at
  `/Users/dio/Library/Caches/ShareMe/webrtc` was preserved and not staged,
  cleaned, or rebuilt.

Windows native media behavior, live native output, route changes, and physical
acoustic/display synchronization remain environment-dependent and are not
claimed by this macOS verification. No video correction, estimator, route API,
Stage 2B measurement, or hard-resync wiring was added.

## Final Whole-Stage Review Ruling

The final whole-Stage-2A review accepted the generation-scope reset, logical
anchor origin, pause/resume wiring, Qt `IdleState` handling, and final handoff
SHA. It raised a defensive edge-case concern that an injected exception from
`AudioOutputDevice::pause()` during a paused media-scope restart could leave an
uncertain backend state, and that a failed resume is not automatically retried.
This was parked as non-load-bearing for the current boundary: the concrete Qt
adapter has no throwing pause path, the renderer marks failed output inactive
and invalidates its clock before further pump writes, and the plan explicitly
classifies failed resume as local renderer output failure rather than an
automatic retry. Normal pause/resume, paused scope change, failed reopen, and
stale-PCM release are covered by the renderer tests. No Stage 2B work started.
