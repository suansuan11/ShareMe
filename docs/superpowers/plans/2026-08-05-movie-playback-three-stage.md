# Movie Playback Three-Stage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved movie playback boundary in four isolated delivery phases: safe software video defaults, Stage 2A audio-clock infrastructure, gated Stage 2B correction, and cross-platform movie-audio route switching.

**Architecture:** Keep `MovieAudioPeer` as the dedicated WebRTC transport and move movie-audio buffering, PTS mapping, output consumption, and video scheduling into Qt-free contracts plus a renderer adapter. `RtcDemoController` only wires lifecycle, protocol, diagnostics, and payload adapters. Stage 2B correction is developed on an unmerged experiment branch and remains explicit opt-in even after a successful gate.

**Tech Stack:** C++20, CMake/CTest, FFmpeg/libswscale, libyuv, locked libwebrtc, Qt 6 Multimedia, Qt `QAudioSink`/`QMediaDevices`, Python 3 study scripts, JSONL diagnostics, CoreAudio, Windows `IMMNotificationClient`, and Linux PipeWire/PulseAudio adapters.

## Global Constraints

- Preserve the locked external libwebrtc cache, manifest, revision, GN arguments, and ABI; do not enable `rtc_use_h264` or replace the VP8 factory.
- Report `webrtc_encoder=vp8-software` and `hardware_encoder_status=unavailable-locked-abi` throughout these stages.
- Keep `client/core` free of Qt, FFmpeg, libwebrtc, GPU SDK, and operating-system headers.
- Keep movie audio, host voice, and viewer voice independent; voice remains on the existing WebRTC ADM.
- Keep WebRTC callbacks nonblocking; callback ingress performs one bounded copy directly into `MovieAudioRenderer` and never waits for Qt or an output device.
- Preserve source/transmitted resolution, cadence, bitrate policy, color metadata, queue bounds, drop policy, audio format, pause/resume, seek, generation, and existing quality gates.
- Use one ignored feature worktree per Stage 1, Stage 2A, Stage 2B experiment, and Stage 3 boundary.
- Do not merge, push, deploy, or delete external caches without user authorization.
- Record platform claims separately as portable, Qt-notification, native-adapter-build, native-device, or physical-acoustic evidence.
- Do not commit build output, generated fixtures, raw media, JSONL, traces, secrets, local settings, or dependency caches.

## Current Base And File Map

The clean planning base is `main` at `27dac81`. The approved design is
`docs/superpowers/specs/2026-08-05-movie-playback-three-stage-design.md`.

### Existing modules to extend

- `client/media/playback/include/shareme/media/ffmpeg_media_source.hpp`: movie decoder mode and default.
- `client/media/playback/include/shareme/media/media_source.hpp`: media metadata and typed decoder-path report.
- `client/media/playback/src/ffmpeg_media_source.cpp`: software/VideoToolbox selection and fallback classification.
- `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp`: movie format report and default constructors.
- `client/rtc/movie/src/movie_video_source.cpp`: source-format propagation.
- `client/rtc/webrtc/src/webrtc_runtime.cpp`: fixed libvpx VP8 factory; no hardware change.
- `client/rtc/webrtc/include/shareme/rtc/movie_audio_peer.hpp` and `client/rtc/webrtc/src/movie_audio_peer.cpp`: transport-only PCM callback and viewer playout policy.
- `client/rtc/movie/include/shareme/rtc/movie_audio_source.hpp` and `client/rtc/movie/src/movie_audio_source.cpp`: host audio source sequence/PTS snapshot.
- `client/tools/rtc_demo/main.cpp`: explicit mode and correction flags.
- `client/tools/rtc_demo/rtc_demo_controller.hpp` and `client/tools/rtc_demo/rtc_demo_controller.cpp`: lifecycle/protocol/diagnostic wiring only.
- `client/tools/rtc_demo/playback_state.*`, `playout_report.*`, and `drift_metrics_jsonl.*`: sanitized protocol and study records.
- `client/tools/rtc_demo/video_preview_adapter.*`: application-level sink presentation only.
- `scripts/run_movie_drift_study.py`: frozen drift study and new audio-clock gate.
- `scripts/run_movie_performance_study.py`: sanitized performance counter allowlist.

### New portable-core modules

- `client/core/include/shareme/core/playback_failure.hpp` and `client/core/src/playback_failure.cpp`: one category-to-string and category-to-impact mapping.
- `client/core/include/shareme/core/audio_output_contract.hpp`: PCM units, output-device facts, write results, final snapshots, and device interface.
- `client/core/include/shareme/core/movie_audio_pts_mapper.hpp` and `client/core/src/movie_audio_pts_mapper.cpp`: anchors, correlation state, residual validation, and generation/epoch checks.
- `client/core/include/shareme/core/movie_audio_clock.hpp` and `client/core/src/movie_audio_clock.cpp`: logical consumption, latency adjustment, confidence, and clock snapshots.
- `client/core/include/shareme/core/movie_audio_renderer.hpp` and `client/core/src/movie_audio_renderer.cpp`: bounded ready/in-flight PCM ownership and output-device handoff.
- `client/core/include/shareme/core/movie_video_scheduler.hpp` and `client/core/src/movie_video_scheduler.cpp`: opaque video tokens, disposition, bounded hold/drop, candidate episodes, and hard-resync exits.
- `client/core/include/shareme/core/audio_route.hpp` and `client/core/src/audio_route.cpp`: route event values and portable route transaction policy.

### New Qt/native modules

- `client/tools/rtc_demo/qt_audio_output_device.hpp` and `.cpp`: `QAudioSink` implementation of `AudioOutputDevice`, including `quiesce_and_snapshot()`.
- `client/tools/rtc_demo/movie_audio_clock_message.hpp` and `.cpp`: sanitized reliable `movie-audio-clock` control envelope.
- `client/tools/rtc_demo/movie_video_playout_adapter.hpp` and `.cpp`: WebRTC frame payload ownership around core opaque tokens.
- `client/tools/rtc_demo/qt_audio_route_monitor.hpp` and `.cpp`: `QMediaDevices` common notification adapter.
- `client/tools/rtc_demo/windows_audio_route_monitor.cpp`: Windows endpoint notification adapter.
- `client/tools/rtc_demo/macos_audio_route_monitor.mm`: CoreAudio default-output notification adapter.
- `client/tools/rtc_demo/linux_audio_route_monitor.cpp`: PipeWire/PulseAudio notification adapter behind platform dependency guards.

### Test modules

- `tests/core/playback_failure_test.cpp`
- `tests/core/audio_output_contract_test.cpp`
- `tests/core/movie_audio_pts_mapper_test.cpp`
- `tests/core/movie_audio_clock_test.cpp`
- `tests/core/movie_audio_renderer_test.cpp`
- `tests/core/movie_video_scheduler_test.cpp`
- `tests/core/audio_route_test.cpp`
- Existing `tests/media/ffmpeg_media_source_test.cpp`, `tests/rtc/movie_video_source_test.cpp`, `tests/rtc/movie_audio_source_test.cpp`, `tests/rtc/movie_audio_peer_test.cpp`, `tests/rtc/playback_state_test.cpp`, `tests/rtc/playout_report_test.cpp`, `tests/rtc/drift_metrics_jsonl_test.cpp`, `tests/scripts/rtc_demo_cli_test.py`, `tests/scripts/movie_drift_study_test.py`, and `tests/scripts/movie_performance_study_test.py`.

## Common Verification Commands

Before execution, set `WEBRTC_ROOT` to the already-existing repository-external
libwebrtc cache path. Do not create, rebuild, rewrite, or remove that cache.

```bash
cmake --fresh --preset movie-call-dev -DWEBRTC_ROOT="$WEBRTC_ROOT"
cmake --build --preset build-movie-call-dev --parallel 4
ctest --test-dir build/movie-call-dev --output-on-failure
git diff --check
git status --short --branch
```

Focused CTest runs use the same configured build tree:

```bash
ctest --test-dir build/movie-call-dev --output-on-failure \
  -R 'ffmpeg_media_source|movie_video_source|movie_audio_source|movie_audio_peer|rtc_demo_cli_contract|playback_state|playout_report|drift_metrics_jsonl'
```

The exact CTest test names are registered by `tests/core/CMakeLists.txt`,
`tests/media/CMakeLists.txt`, `tests/rtc/CMakeLists.txt`, and
`tests/scripts/CMakeLists.txt`. A skipped platform test is reported as
environment-dependent, not verified.

## Stage 1: Safe Video-Path Boundary

**Boundary:** one Stage 1 worktree and one Stage 1 merge/rollback point.

**Rollback point:** the Stage 1 branch parent `27dac81`.

### Task 1.1: Write failing software-default and capability tests

**Files:**
- Modify: `tests/media/ffmpeg_media_source_test.cpp`
- Modify: `tests/rtc/movie_video_source_test.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `tests/scripts/movie_performance_study_test.py`

**Interfaces:**
- The tests will expect `VideoAccelerationMode::software` as the default.
- The tests will expect a typed decoder path report with requested mode,
  `software|hardware|fallback`, and a fixed WebRTC report of
  `vp8-software`/`unavailable-locked-abi`.

- [ ] Add a media-source test that opens the generated fixture with default
  options and asserts requested mode `software` and decoder path `software`.
- [ ] Add an explicit-`auto` test that asserts the request remains `auto` and
  the actual path is reported independently from the WebRTC encoder report.
- [ ] Add a CLI contract case that omits `--video-acceleration` and verifies
  validation succeeds with the software default, while explicit `auto` remains
  accepted and `hardware` remains rejected.
- [ ] Add parser fixtures for the four sanitized fields without accepting a
  combined ambiguous `path=hardware` encoder claim.
- [ ] Run the focused CTest command. It must fail because the current defaults
  are `auto` and the new typed report fields do not exist.

### Task 1.2: Implement typed decoder-path reporting and software defaults

**Files:**
- Create: `client/media/playback/include/shareme/media/video_path.hpp`
- Modify: `client/media/playback/include/shareme/media/ffmpeg_media_source.hpp`
- Modify: `client/media/playback/include/shareme/media/media_source.hpp`
- Modify: `client/media/playback/src/ffmpeg_media_source.cpp`
- Modify: `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp`
- Modify: `client/rtc/movie/src/movie_video_source.cpp`
- Modify: `client/media/playback/CMakeLists.txt`

**Interfaces:**
- Define `VideoAccelerationMode { software, auto_mode }` in `video_path.hpp`.
- Define `VideoDecoderPath { software, hardware, fallback }` and
  `VideoPathReport { requested, decoder }` in the same header.
- Add `VideoPathReport video_path` to `MediaInfo` and propagate it through
  `MovieVideoFormat`; retain the existing string only as the serialized
  compatibility value for current performance counters.
- Change every decoder/source default to `VideoAccelerationMode::software`.
- Make `open_decoder` return whether hardware was attempted, selected, or
  fell back so an explicit software request reports `software` while an
  `auto` request that tried and abandoned VideoToolbox reports `fallback`.

- [ ] Move the enum without changing the public option spelling `auto|software`.
- [ ] Add the typed report to `MediaInfo`, initialize it to software, and set
  `fallback` only after an explicitly requested `auto` hardware attempt fails
  before software decoding succeeds.
- [ ] Update `MovieVideoSource` constructors and format snapshots to propagate
  the typed report without changing frame dimensions, cadence, color, or queue
  behavior.
- [ ] Run `ctest --test-dir build/movie-call-dev --output-on-failure -R 'ffmpeg_media_source|movie_video_source'` and confirm Task 1.1 turns green.

### Task 1.3: Implement fixed WebRTC codec capability reporting

**Files:**
- Create: `client/rtc/webrtc/include/shareme/rtc/video_codec_report.hpp`
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `client/rtc/webrtc/src/webrtc_runtime.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `scripts/run_movie_performance_study.py`

**Interfaces:**
- Define `VideoCodecReport { encoder, hardware_encoder_status }` with the only
  current values `vp8-software` and `unavailable-locked-abi`.
- Expose the report from `SignaledPeer` as immutable capability data; do not
  add an encoder factory callback or platform adapter in this stage.
- Add distinct sanitized fields `requested_mode`, `decoder_path`,
  `webrtc_encoder`, and `hardware_encoder_status` to performance output.

- [ ] Keep `webrtc_runtime.cpp` registering only the existing libvpx VP8
  factory and add no H.264 or hardware code.
- [ ] Change the CLI default value to `software` while preserving explicit
  `auto` validation.
- [ ] Make the controller join media and WebRTC reports without putting the
  scheduling algorithm or clock state in the controller.
- [ ] Extend the Python allowlist and parser tests for the separate fields,
  including rejection of paths, negative values, unknown capability values, and
  fabricated hardware encoder values.
- [ ] Run the Stage 1 focused tests and the script contracts.

### Task 1.4: Stage 1 verification and commit

- [ ] Run the common configure/build commands.
- [ ] Run the Stage 1 focused CTest command and full CTest.
- [ ] Run the registered `rtc_demo_cli_contract`,
  `movie_performance_study_contract`, and
  `video_quality_contract_portability` CTest tests for the complete configured
  invocations.
- [ ] Verify `git diff --check`, `git status`, no external cache changes, and
  no generated output staged.
- [ ] Commit the complete Stage 1 boundary with focused commits such as
  `test: lock software movie video default` and
  `feat: report movie decoder and VP8 capability`.
- [ ] Modify `docs/development/current-stage.md` only at this completed stage
  boundary and record the accepted Stage 1 SHA.

## Stage 2A: Renderer, Clock, Candidate Scheduler

**Boundary:** one Stage 2A worktree based on the accepted Stage 1 commit.

**Rollback point:** the recorded Stage 1 acceptance commit. If the correlation
checkpoint is blocked, Stage 2A may merge with candidate-only behavior and the
next rollback remains that Stage 1 acceptance commit.

### Task 2A.1: Write failing portable contracts and failure taxonomy tests

**Files:**
- Create: `tests/core/playback_failure_test.cpp`
- Create: `tests/core/audio_output_contract_test.cpp`
- Create: `tests/core/movie_audio_pts_mapper_test.cpp`
- Modify: `client/core/CMakeLists.txt`
- Modify: `tests/core/CMakeLists.txt`

**Interfaces:**
- `PlaybackCategory` maps through one `playback_category_name()` and
  `playback_category_impact()` implementation.
- `AudioPcmBlockView` defines `receiver_sequence`, `frame_count`,
  `sample_rate`, `channel_count`, `sample_format`, and `interleaving`.
- `WriteResult` distinguishes positive per-call `accepted_frames`, zero-frame
  `would_block`, and zero-frame `failed`.
- `AudioDeviceSnapshot` contains per-device `device_instance_id`,
  `snapshot_sequence`, accepted/consumed totals, queue frames, optional
  latency, underrun/discontinuity counters, and active state.
- `FinalDeviceSnapshot` adds `quiesced=true` and `exact_consumption`.

- [ ] Test every requested failure string and impact, including
  `early-hold-limit`, `route-candidate-stale`, `audio-output-device-lost`, and
  `hard-resync-attempt-limit`.
- [ ] Test that one audio frame is one sample per channel and duration is
  `frame_count / sample_rate`.
- [ ] Test per-call versus cumulative output counts and zero-frame write
  status distinction.
- [ ] Test anchor rejection for control sequence regression, playback
  generation regression, audio epoch regression, format change, and residual
  overflow.
- [ ] Run the new core tests and confirm they fail because the contracts do not
  exist.

### Task 2A.2: Implement portable output facts, PTS mapper, and audio clock

**Files:**
- Create: `client/core/include/shareme/core/playback_failure.hpp`
- Create: `client/core/src/playback_failure.cpp`
- Create: `client/core/include/shareme/core/audio_output_contract.hpp`
- Create: `client/core/include/shareme/core/movie_audio_pts_mapper.hpp`
- Create: `client/core/src/movie_audio_pts_mapper.cpp`
- Create: `client/core/include/shareme/core/movie_audio_clock.hpp`
- Create: `client/core/src/movie_audio_clock.cpp`
- Modify: `client/core/CMakeLists.txt`

**Interfaces:**
- `MovieAudioPtsMapper::accept_anchor(AudioAnchor)` returns a value result and
  never locks on a control anchor alone.
- `MovieAudioPtsMapper::observe_correlation(CorrelationObservation)` promotes
  confidence only for a validated shared source/decoded sequence or an
  explicitly approved estimator.
- `MovieAudioClock::observe(AudioClockObservation, monotonic_now)` produces
  `MovieAudioClockSnapshot` with confidence
  `unavailable|provisional|locked|degraded|invalid`.
- `MovieAudioClock` uses `logical_consumed_frames`, trusted output latency,
  playback generation, host audio epoch, renderer clock epoch, and route
  generation without using bytes written or QVideoSink timestamps.

- [ ] Implement checked frame-to-millisecond arithmetic and monotonic PTS
  validation per playback generation and renderer clock epoch.
- [ ] Make unknown consumption stop logical progress at the last provable value,
  increment the renderer clock epoch, and invalidate confidence.
- [ ] Keep all headers standard-library-only and verify `client/core` has no Qt,
  FFmpeg, WebRTC, GPU, or OS include.
- [ ] Run `ctest --test-dir build/movie-call-dev --output-on-failure -R 'playback_failure|audio_output_contract|movie_audio_pts_mapper'`.

### Task 2A.3: Implement renderer ownership with a Qt output adapter

**Files:**
- Create: `client/core/include/shareme/core/movie_audio_renderer.hpp`
- Create: `client/core/src/movie_audio_renderer.cpp`
- Create: `client/tools/rtc_demo/qt_audio_output_device.hpp`
- Create: `client/tools/rtc_demo/qt_audio_output_device.cpp`
- Create: `tests/core/movie_audio_renderer_test.cpp`
- Modify: `client/core/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `tests/core/CMakeLists.txt`

**Interfaces:**
- `MovieAudioRenderer::try_enqueue(AudioPcmBlockView, receiver_sequence)` is
  nonblocking and performs the only callback-to-renderer PCM copy.
- `MovieAudioRenderer::pump(monotonic_now)` writes ready blocks through an
  injected `AudioOutputDevice` and tracks ready versus in-flight duration.
- `MovieAudioRenderer::activate_output(unique_ptr<AudioOutputDevice>)` commits
  only an already active device; `quiesce_output()` uses the device's atomic
  `quiesce_and_snapshot()` operation.
- `QtAudioOutputDevice` uses 48 kHz, stereo, interleaved signed 16-bit PCM,
  `QAudioSink::processedUSecs()` for device consumption, `bytesFree()` for
  device queue facts, and explicit unavailable latency when it cannot be
  trusted.

- [ ] Add fake output devices that produce positive writes, would-block, failure,
  underrun, exact final snapshots, partial consumption, unknown consumption,
  candidate loss, and delayed snapshots.
- [ ] Test ready/in-flight queue bounds, overflow discontinuity, exact suffix
  trimming, replay counters, cumulative media/backend counters, and exactly-once
  PCM block release.
- [ ] Test PCM callback ingress during renderer teardown completes within the
  bounded callback budget and never waits.
- [ ] Implement the Qt adapter without placing Qt headers in `client/core`.
- [ ] Run `ctest --test-dir build/movie-call-dev --output-on-failure -R 'movie_audio_renderer'` and the Qt offscreen test.

### Task 2A.4: Add transport callback and provisional anchor protocol

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/local_audio_source.hpp`
- Modify: `client/rtc/movie/include/shareme/rtc/movie_audio_source.hpp`
- Modify: `client/rtc/movie/src/movie_audio_source.cpp`
- Modify: `client/rtc/webrtc/include/shareme/rtc/movie_audio_peer.hpp`
- Modify: `client/rtc/webrtc/src/movie_audio_peer.cpp`
- Create: `client/tools/rtc_demo/movie_audio_clock_message.hpp`
- Create: `client/tools/rtc_demo/movie_audio_clock_message.cpp`
- Modify: `client/tools/rtc_demo/playback_state.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/rtc/movie_audio_source_test.cpp`
- Modify: `tests/rtc/movie_audio_peer_test.cpp`
- Create: `tests/rtc/movie_audio_clock_message_test.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Modify: `client/rtc/movie/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Add a defaulted `LocalAudioSource::clock_snapshot()` returning
  `LocalAudioSourceClockSnapshot { source_sequence, media_pts_ms, generation,
  audio_epoch, sample_rate, channel_count }` so existing fake sources remain
  valid.
- Add `MovieAudioPeerCallbacks::pcm(AudioPcmBlockView, receiver_sequence)`.
  `MovieAudioPeer` forwards the synchronous WebRTC callback and owns no queue.
- Expose `MovieAudioPeer::source_clock_snapshot()` for host anchor creation.
- Define a reliable ordered `movie-audio-clock` message with room, positive
  control sequence, playback generation, audio epoch, host source sequence,
  media PTS, sample rate, and channel count.

- [ ] Make `MovieAudioSource` report its timeline generation, audio epoch,
  generated chunk sequence, and last media PTS without copying PCM.
- [ ] Set viewer movie peer `native_playout=false`; leave voice ADM setup
  untouched.
- [ ] Have the WebRTC audio sink invoke renderer ingress directly and return
  without waiting for Qt or output operations.
- [ ] Add encode/decode validation for stale sequence, wrong room, format
  mismatch, generation regression, and JSON-safe bounds.
- [ ] Keep the production mapper provisional until a correlation value exists;
  prove that anchors alone cannot promote it to `locked`.
- [ ] Run `ctest --test-dir build/movie-call-dev --output-on-failure -R 'movie_audio_source|movie_audio_peer|movie_audio_clock_message'`.

### Task 2A.5: Add observational video token scheduler and diagnostics

**Files:**
- Create: `client/core/include/shareme/core/movie_video_scheduler.hpp`
- Create: `client/core/src/movie_video_scheduler.cpp`
- Create: `client/tools/rtc_demo/movie_video_playout_adapter.hpp`
- Create: `client/tools/rtc_demo/movie_video_playout_adapter.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/playout_report.hpp`
- Modify: `client/tools/rtc_demo/playout_report.cpp`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Create: `tests/core/movie_video_scheduler_test.cpp`
- Modify: `tests/rtc/playout_report_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`
- Modify: `client/core/CMakeLists.txt`

**Interfaces:**
- `MovieVideoPlayoutScheduler::submit(VideoFrameTiming)` accepts an opaque
  token and returns a disposition plus removed tokens.
- `MovieVideoPlayoutScheduler::advance(VideoClockInput)` returns separate
  frame dispositions and scheduler-level events.
- `VideoFrameTiming.media_pts_ms` is produced by a core wrap-safe RTP/video
  anchor mapper; the controller does not calculate scheduling policy.
- The report carries `viewer_suggested_action`, `viewer_applied_action`,
  `audio_clock_confidence`, `audio_playout_pts_ms`, `logical_consumed_frames`,
  `renderer_queue_duration`, `device_queue_duration`, `route_generation`, and
  renderer clock epoch values.

- [ ] Test unavailable/provisional/degraded/invalid clock pass-through before
  testing locked-clock policy.
- [ ] Test exact thresholds: early enter `+50`, early exit `+25`, late enter
  `-50`, late exit `-25`, hard candidate `-300`, cancellation above `-250`.
- [ ] Test severe positive delta bounded to three tokens or 250 ms, whichever
  comes first, then `clock_blocked` pass-through of all held tokens.
- [ ] Test periodic candidate qualification, generation/route/confidence/
  discontinuity/pause/sequence resets, token release reasons, and no QVideoSink
  timestamp use.
- [ ] Keep production disposition observational/pass-through in Stage 2A while
  policy tests exercise pure hold/drop/resync decisions.
- [ ] Run `ctest --test-dir build/movie-call-dev --output-on-failure -R 'movie_video_scheduler|playout_report|video_preview_adapter'`.

### Task 2A.6: Stage 2A integration, checkpoint, and commit

- [ ] Add the renderer and scheduler to `RtcDemoController` only as lifecycle
  orchestration: construct, enqueue, pump, publish anchors, publish reports,
  and stop in dependency-safe order.
- [ ] Replace the old viewer `native_playout=true` contract test with an
  app-owned renderer contract test; assert primary voice ADM/peer identities are
  unchanged.
- [ ] Add deterministic video-stall injection tests for 500 ms and 2 s stalls.
  Assert bounded token count, candidate telemetry, audio queue continuity, and
  no production correction while confidence is provisional.
- [ ] Run focused CTest, full CTest, `git diff --check`, and the registered
  `rtc_demo_cli_contract`, `movie_drift_study_contract`, and
  `movie_performance_study_contract` tests.
- [ ] Run the portable contract scan that confirms `client/core` has no Qt,
  FFmpeg, WebRTC, GPU, or OS include.
- [ ] Record the correlation feasibility result. If no shared value or approved
  estimator exists, record `blocked-on-audio-correlation`, commit Stage 2A,
  and stop before Stage 2B measurement.
- [ ] When the result is `blocked-on-audio-correlation`, do not start Task 2B.2
  or Task 2B.3; leave no correction wiring enabled and preserve the blocked
  result as the Stage 2B rollback evidence.
- [ ] Commit Stage 2A with focused commits such as
  `feat: add app-owned movie audio renderer` and
  `feat: add observational movie video scheduler`.
- [ ] Modify `docs/development/current-stage.md` only at this completed stage
  boundary and record the accepted Stage 2A SHA.
- [ ] Record the accepted Stage 2A SHA and correlation result in the handoff.

## Stage 2B: Gated Correction Experiment

**Boundary:** a separate experiment worktree/branch based on the accepted Stage
2A commit. It is not part of the Stage 2A merge boundary.

**Rollback point:** the recorded Stage 2A acceptance commit. If correlation is
blocked or any gate fails, leave the experiment branch and artifacts as
evidence, keep it unmerged, and return future product work to Stage 2A.

### Task 2B.1: Add correlation feasibility and Audio Clock Gate tests first

**Files:**
- Create: `tests/core/audio_clock_gate_test.cpp`
- Modify: `tests/core/movie_audio_pts_mapper_test.cpp`
- Modify: `tests/core/movie_audio_clock_test.cpp`
- Modify: `tests/rtc/drift_metrics_jsonl_test.cpp`
- Modify: `tests/scripts/movie_drift_study_test.py`
- Modify: `scripts/run_movie_drift_study.py`
- Modify: `client/core/include/shareme/core/drift_metrics.hpp`
- Modify: `client/core/src/drift_metrics.cpp`
- Modify: `client/tools/rtc_demo/drift_metrics_jsonl.cpp`

**Interfaces:**
- Add an explicit `CorrelationResult` with `source_sequence`,
  `decoded_sequence`, `residual_ms`, `valid`, and `reason`.
- Add audio-clock fields to the drift sample schema: clock confidence,
  audio PTS, logical consumed frames, renderer/device queue durations, route and
  renderer epochs, correlation residual, underrun/discontinuity totals, and
  suggested/applied actions.
- Keep schema versioning explicit; update the JSONL writer, parser, and tests
  together so old samples cannot silently pass a new gate.

- [ ] Add passing/failing synthetic three-run reports for every Audio Clock
  Gate threshold: 99.0% locked coverage, 99.0% correlation coverage, zero
  uncorrelated blocks, residual P95/P99/max of 20/40/80 ms, zero unexpected
  epoch changes, zero clean-window underruns/discontinuities, monotonic PTS,
  reference error P95/P99/max of 20/40/80 ms, and route relock within 1,000 ms.
- [ ] Add a feasibility test that leaves the mapper provisional when only a
  DataChannel anchor is present.
- [ ] Add exact hard-resync candidate tests for four ordered observations over
  at least 750 ms, 10-second cooldown, three-attempt limit, and all reset causes.
- [ ] Run the new tests and confirm they fail against the Stage 2A candidate
  telemetry before changing production correction wiring.

### Task 2B.2: Implement auditable candidate correction wiring

**Files:**
- Modify: `client/core/src/movie_video_scheduler.cpp`
- Modify: `client/core/include/shareme/core/movie_video_scheduler.hpp`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/movie_video_playout_adapter.cpp`
- Modify: `client/tools/rtc_demo/playout_report.*`
- Modify: `client/tools/rtc_demo/qml/Main.qml`

**Interfaces:**
- Add an explicit `--enable-video-correction` option, default false and valid
  only for the movie viewer path.
- Apply `FrameDisposition::hold`/`drop` only when the flag is set and the audio
  clock is `locked`; otherwise emit `clock_blocked`/candidate telemetry.
- Qualify hard resync only from periodic observations, then enforce 10-second
  cooldown and three automatic attempts per call.
- Use viewer-local bounded reacquisition with two-second timeout and 120-frame
  limit. Emit `viewer_applied_action=hard-resync` only after successful same-
  generation reacquisition at or after audio PTS.
- Use the frozen exit precedence: generation change, route transition, clock
  loss, EOS, frame limit, timeout, applied.

- [ ] Keep the experiment branch source and binary SHA in every study artifact.
- [ ] Keep correction default-off even if the experiment branch passes all
  tests; do not change the default without a separate user request.
- [ ] Update static CLI/QML/controller tests so suggested and applied actions
  are distinct and candidate actions never appear as applied.
- [ ] Run focused scheduler/report/CLI tests before any real study.

### Task 2B.3: Run the auditable drift, audio-clock, and quality gates

- [ ] Build the experiment branch and record the exact demo SHA:

```bash
cmake --fresh --preset movie-call-dev -DWEBRTC_ROOT="$WEBRTC_ROOT"
cmake --build --preset build-movie-call-dev --parallel 4
shasum -a 256 build/movie-call-dev/client/tools/rtc_demo/shareme_rtc_demo
```

- [ ] Run exactly three sequential drift studies into an ignored output root:

```bash
python3 scripts/run_movie_drift_study.py \
  --demo build/movie-call-dev/client/tools/rtc_demo/shareme_rtc_demo \
  --server-url ws://127.0.0.1:18080/v1/ws \
  --server-root build/movie-call-dev/stage2b-drift-server \
  --movie "$MOVIE_PATH" \
  --output-parent build/movie-call-dev \
  --output-root build/movie-call-dev/stage2b-drift-study \
  --run-count 3
```

- [ ] Run the frozen quality-preserving performance study with distinct
  software identities and the existing 180-second duration:

```bash
python3 scripts/run_movie_performance_study.py \
  --output-parent build/movie-call-dev \
  --output-root build/movie-call-dev/stage2b-performance-study \
  --run-count 3 \
  --demo build/movie-call-dev/client/tools/rtc_demo/shareme_rtc_demo \
  --server-url ws://127.0.0.1:18080/v1/ws \
  --server-root build/movie-call-dev/stage2b-performance-server \
  --movie "$MOVIE_PATH" \
  --video-acceleration software \
  --duration-seconds 180
```

- [ ] Confirm every run has a complete artifact, source/binary SHA, Audio Clock
  Gate pass, frozen drift gate pass, quality gate pass, zero unplanned
  discontinuities, and no missing correlation evidence.
- [ ] If any gate fails, record the exact failed metric and leave Stage 2B
  unmerged. Do not tune thresholds, enable correction, or reinterpret missing
  evidence as success.
- [ ] If every gate passes, run focused tests, full CTest, `git diff --check`,
  and request the separate user-authorized Stage 2B merge. Keep correction
  explicit opt-in after that merge.
- [ ] Modify `docs/development/current-stage.md` only after the authorized
  Stage 2B merge, recording either the accepted merge SHA or the blocked
  experiment result.

## Stage 3: Cross-Platform Movie-Audio Routing

**Boundary:** one Stage 3 worktree based on the accepted Stage 2A commit or the
separately accepted Stage 2B merge.

**Rollback point:** the recorded Stage 2B acceptance merge if it exists;
otherwise the recorded Stage 2A acceptance commit. Route work must be removed
without changing the movie transport, video peer, or voice ADM.

### Task 3.1: Write failing portable route transaction tests

**Files:**
- Create: `client/core/include/shareme/core/audio_route.hpp`
- Create: `client/core/src/audio_route.cpp`
- Create: `tests/core/audio_route_test.cpp`
- Modify: `client/core/CMakeLists.txt`
- Modify: `tests/core/CMakeLists.txt`

**Interfaces:**
- `AudioRouteEvent { event_sequence, stable_device_id, change_kind,
  default_role, observed_at }` uses opaque in-memory identity only.
- `AudioRouteMonitor` exposes `start(callback(AudioRouteEvent))` and `stop`.
- `AudioRouteController` rejects stale event sequences/candidate identity and
  commits `route_generation` only after candidate activation succeeds.

- [ ] Test notification without generation change, duplicate/coalesced event,
  stale event, candidate loss, shutdown event, and repeated route changes.
- [ ] Test exact handoff, unknown consumption, old-route resume, no active
  output, and route-transition video behavior with three-frame/250 ms first-hit
  bounds.
- [ ] Run the new route test and confirm it fails before the route policy exists.

### Task 3.2: Implement atomic output handoff and lifecycle accounting

**Files:**
- Modify: `client/core/src/movie_audio_renderer.cpp`
- Modify: `client/core/include/shareme/core/movie_audio_renderer.hpp`
- Modify: `client/tools/rtc_demo/qt_audio_output_device.hpp`
- Modify: `client/tools/rtc_demo/qt_audio_output_device.cpp`
- Modify: `tests/core/movie_audio_renderer_test.cpp`
- Modify: `tests/core/audio_route_test.cpp`

**Interfaces:**
- Route handoff calls `AudioOutputDevice::quiesce_and_snapshot()` rather than
  `pause()` followed by a later snapshot.
- `FinalDeviceSnapshot` must be immutable, quiesced, identity-tagged, sequence-
  tagged, and explicit about exact consumption.
- Exact handoff retires fully consumed frames, trims the partial suffix, starts
  the candidate, requeues the suffix, commits the candidate, then increments
  `route_generation`.
- Unknown consumption increments `renderer_clock_epoch`, leaves logical
  consumption non-decreasing at the last provable value, invalidates the clock,
  and prevents correction until re-lock.

- [ ] Test renderer/device duration separation, replay counters, stale final
  snapshots, immediate candidate loss, output-device lost, callbacks during
  teardown, and exactly-once token/PCM release.
- [ ] Test that failed activation resumes the old output when possible and
  never increments `route_generation`.
- [ ] Test that neither the `MovieAudioPeer` identity nor voice ADM/transport
  state changes during any handoff.
- [ ] Run focused core and Qt offscreen tests.

### Task 3.3: Implement Qt and platform route monitors

**Files:**
- Create: `client/tools/rtc_demo/qt_audio_route_monitor.hpp`
- Create: `client/tools/rtc_demo/qt_audio_route_monitor.cpp`
- Create: `client/tools/rtc_demo/windows_audio_route_monitor.cpp`
- Create: `client/tools/rtc_demo/macos_audio_route_monitor.mm`
- Create: `client/tools/rtc_demo/linux_audio_route_monitor.cpp`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`

**Interfaces:**
- Qt `QMediaDevices` supplies the common output-list/default-output event.
- Windows uses `IMMNotificationClient::OnDefaultDeviceChanged`.
- macOS uses a CoreAudio default-output property listener.
- Linux uses the available PipeWire/PulseAudio route notification adapter; the
  build must report missing native dependencies as environment-dependent rather
  than compiling a fake adapter.
- Every adapter emits the common `AudioRouteEvent` and no device identifier is
  written to sanitized logs.

- [ ] Test Qt notification delivery and event-sequence monotonicity without
  hardware output changes.
- [ ] Add platform-guarded native adapter build tests; keep platform headers out
  of `client/core`.
- [ ] Verify route notifications during shutdown are ignored after ingress is
  closed and callback completion remains bounded.
- [ ] Run platform-specific build checks separately for macOS, Windows, and
  Linux; do not label one platform's adapter as another platform's proof.

### Task 3.4: Integrate route lifecycle and diagnostics

**Files:**
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `tests/rtc/CMakeLists.txt`

- [ ] Start the common monitor after renderer creation and stop it before peer
  teardown; keep the video peer, movie-audio peer, and voice ADM alive during
  route replacement.
- [ ] Expose route generation, clock confidence, renderer/device queue
  durations, suggested action, applied action, underrun count, and stable
  discontinuity category as diagnostics only.
- [ ] Ensure route transition enters its bounded scheduler state and never uses
  stale pre-switch audio PTS.
- [ ] Update CLI/static tests from native viewer movie playout to app-owned
  renderer ownership and separate suggested/applied fields.
- [ ] Verify no device identifiers, room IDs, SDP, ICE addresses, or paths enter
  sanitized diagnostics.

### Task 3.5: Stage 3 verification and commit

- [ ] Run the common configure/build commands and focused route/render tests.
- [ ] Run full CTest and `git diff --check`.
- [ ] Run Qt notification verification on macOS and record it separately from
  native-device switching and physical acoustic behavior.
- [ ] Run Windows native endpoint build/manual switching only on Windows; label
  it environment-dependent when unavailable.
- [ ] Run Linux PipeWire/PulseAudio build/notification checks only where the
  dependencies exist; do not claim Linux hardware behavior from macOS.
- [ ] Verify repeated route changes, candidate loss, old-route resume, audio
  clock re-lock, no stale correction, unchanged voice state, and output
  continuity within the frozen Audio Clock Gate thresholds.
- [ ] Commit Stage 3 with focused commits such as
  `feat: add movie audio route handoff` and
  `feat: add platform audio route monitors`.
- [ ] Modify `docs/development/current-stage.md` only at this completed stage
  boundary and record the accepted Stage 3 SHA.
- [ ] Record the accepted Stage 3 SHA and platform evidence in the handoff.

## Final Review And Rollback Matrix

| Boundary | Commit/branch rule | Rollback point | Required evidence |
| --- | --- | --- | --- |
| Stage 1 | Isolated Stage 1 worktree; no hardware code | `27dac81` | software default, typed decoder path, VP8 capability, full CTest |
| Stage 2A | Isolated Stage 2A worktree; candidate-only production | recorded Stage 1 acceptance SHA | renderer, clock, provisional block, scheduler policy, stall tests, full CTest |
| Stage 2B | Separate unmerged experiment branch; exact source/binary SHA | recorded Stage 2A acceptance SHA | correlation checkpoint, Audio Clock Gate, drift gate, quality gate |
| Stage 3 | Isolated route worktree; no peer/voice rebuild | Stage 2B merge SHA or Stage 2A SHA | route transaction, platform-scoped adapter/build/manual evidence |

If a stage fails, preserve its sanitized evidence and exact failure category,
leave the failed stage unmerged, and continue only from its recorded rollback
point. Do not delete the external libwebrtc cache or generated evidence without
separate authorization.

## Hardware-Encoding Follow-Up

Do not implement hardware encoding in this plan. After Stage 3, prepare a
separate proposal/branch for a new locked WebRTC manifest/revision only if
authorized. That proposal must cover reproducible dependency bootstrap, macOS
VideoToolbox, Windows Media Foundation/D3D11, codec negotiation and fallback,
native-frame transport, and resolution/cadence/bitrate/color/PSNR/SSIM/CPU/RSS/
thermal/audio-clock gates.
