# Receiver Movie Audio Playout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play the dedicated received movie-audio track through the viewer's platform-default speaker while preserving deterministic headless transport tests.

**Architecture:** Add a native playout-only ADM mode, opt a viewer `MovieAudioPeer` into that mode, and enable it only in the Qt RTC demo. Keep host recording disabled, CLI smoke on a discard renderer, and primary voice/video/control behavior unchanged.

**Tech Stack:** C++20, libwebrtc AudioDeviceModule, Qt 6/QML, FFmpeg, CMake/CTest, Python smoke tests, Go signaling.

## Global Constraints

- Movie audio, host voice, and viewer voice remain independent paths.
- Native playout never initializes recording or requests microphone permission.
- Headless automated calls must not depend on an installed speaker device.
- Failures are typed/sanitized and never expose media paths or raw SDP.
- Preserve the external libwebrtc cache read-only.
- Record macOS evidence separately from Windows and manual audible acceptance.

---

### Task 1: Native playout-only audio device

**Files:**
- Modify: `client/rtc/webrtc/src/audio_device_factory.hpp`
- Modify: `client/rtc/webrtc/src/audio_device_factory.cpp`
- Modify: `tests/rtc/audio_device_factory_test.cpp`

**Interfaces:**
- Produces: `AudioDeviceMode::playout`, `RemotePlayoutPolicy::native`, and a `create_audio_device(..., AudioDeviceMode::playout)` result whose ADM is initialized for stereo playout but not recording.
- Consumes: existing `NativeAudioDeviceFactory`, `NativeAudioDeviceInitializer`, and typed `AudioDeviceError` seams.

- [ ] **Step 1: Write failing playout-mode tests**

Add tests requiring a fake native ADM initializer to select/init stereo playout, leave recording uninitialized, return `RemotePlayoutPolicy::native`, skip microphone permission preflight, and preserve dependency/initialization failures without synthetic fallback.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_audio_device_factory_test
```

Expected: compilation fails because `AudioDeviceMode::playout` and native remote playout do not exist.

- [ ] **Step 3: Implement minimal playout initialization**

Extend the mode switch and native initializer branch. The default initializer selects the platform-default output, calls `InitSpeaker()`, `SetStereoPlayout(true)`, and `InitPlayout()`, then requires `PlayoutIsInitialized()` and rejects any recording initialization.

- [ ] **Step 4: Run GREEN**

```bash
cmake --build --preset build-movie-call-dev --target shareme_audio_device_factory_test
ctest --preset test-movie-call-dev -R '^audio_device_factory$' --output-on-failure
git diff --check
```

- [ ] **Step 5: Commit**

```bash
git add client/rtc/webrtc/src/audio_device_factory.* tests/rtc/audio_device_factory_test.cpp
git commit -m "feat: add native movie audio playout device"
```

### Task 2: Opt the GUI viewer into native movie playout

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/movie_audio_peer.hpp`
- Modify: `client/rtc/webrtc/src/movie_audio_peer.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/rtc/movie_audio_peer_test.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Consumes: Task 1 `AudioDeviceMode::playout` and `RemotePlayoutPolicy::native`.
- Produces: `MovieAudioPeerConfig::native_playout`; valid only for viewers. The Qt viewer sets it true, while hosts and `shareme_signaled_call_probe` retain false.

- [ ] **Step 1: Write failing peer and GUI contracts**

Require host + `native_playout=true` to be rejected. Add a test seam or observable policy requiring an enabled viewer to choose playout mode and call `SetAudioPlayout(true)`, while the default viewer remains discard-only. Require the RTC controller source to opt its viewer movie peer into native playout.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_audio_peer_test shareme_rtc_demo
ctest --preset test-movie-call-dev -R '^(movie_audio_peer|rtc_demo_cli_contract)$' --output-on-failure
```

Expected: the new config and controller contract are absent.

- [ ] **Step 3: Implement viewer-only policy**

Choose `AudioDeviceMode::playout` only for a viewer with `native_playout`; otherwise keep `synthetic`. Call `SetAudioRecording(false)` for both roles and `SetAudioPlayout(config_.native_playout)` on the dedicated PeerConnection. Set the RTC demo viewer flag true; do not change the signaled-call probe.

- [ ] **Step 4: Run GREEN and regression loops**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_audio_peer_test shareme_rtc_demo shareme_signaled_call_probe
ctest --preset test-movie-call-dev -R '^(audio_device_factory|movie_audio_peer|signaled_call_cli_contract|rtc_demo_cli_contract)$' --repeat until-fail:10 --output-on-failure
git diff --check
```

- [ ] **Step 5: Commit**

```bash
git add client/rtc/webrtc client/tools/rtc_demo tests/rtc tests/scripts/rtc_demo_cli_test.py
git commit -m "feat: play received movie audio on viewer"
```

### Task 3: Repair adaptive-video smoke and close real-media evidence

**Files:**
- Modify: `scripts/run_signaled_call_smoke.py`
- Modify: `tests/scripts/signaled_call_smoke_test.py`
- Modify: `docs/development/current-stage.md`
- Create: `docs/verification/receiver-movie-audio-playout.md`

**Interfaces:**
- Consumes: unchanged stable `RESULT width=... height=...` and movie-audio metrics.
- Produces: `valid_movie_dimensions(width, height)` accepting positive even 16:9 adaptive output, including 320x180 and 960x540, while rejecting invalid aspect ratios and zero sizes.

- [ ] **Step 1: Write failing adaptive-dimension tests**

Require 320x180 and 960x540 to pass, and 0x0, odd sizes, and non-16:9 output to fail with the existing generic smoke error contract.

- [ ] **Step 2: Run RED**

```bash
ctest --preset test-movie-call-dev -R '^signaled_call_smoke_contract$' --output-on-failure
```

Expected: 960x540 is rejected by the current fixed-size validation.

- [ ] **Step 3: Implement the narrow validator repair**

Replace the `(width, height) == (320, 180)` check with positive, even, exact `width * 9 == height * 16` validation. Keep the minimum 20 received movie frames and all audio assertions unchanged.

- [ ] **Step 4: Run generated and supplied-media acceptance**

```bash
ctest --preset test-movie-call-dev -R '^signaled_call_smoke_contract$' --output-on-failure
python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server --port 18411 --audio microphone --video movie \
  --movie-audio --movie '/Users/dio/Downloads/Media/紫罗兰的永恒花园TV（2018）内封简日字幕 4K（Ma10p x265 flac ass）/01.mkv'
```

Require nonzero primary video/voice, at least 100 valid stereo movie callbacks, zero invalid callbacks, nonzero peak, and no captured codec-collision/race diagnostics.

- [ ] **Step 5: Run complete stage gates**

```bash
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev --output-on-failure
(cd server && go test -count=1 -race ./... && go vet ./...)
python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py
python3 scripts/validate_shareme_skill.py
git diff --check
```

- [ ] **Step 6: Document and commit**

Correct the stale merge status, record automated real-file decode/transport separately from manual audible output and Windows evidence, then commit:

```bash
git add scripts tests/scripts docs
git commit -m "test: verify adaptive real movie transport"
```
