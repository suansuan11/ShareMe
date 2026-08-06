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
