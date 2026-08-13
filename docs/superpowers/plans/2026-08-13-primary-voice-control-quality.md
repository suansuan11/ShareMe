# Primary Voice Control and Quality Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add truthful primary-voice volume control, microphone activity, and conservative call-quality diagnostics, with an audio-only acceptance path that never launches MotionFixture.

**Architecture:** `SignaledPeer` retains the primary audio device for normalized speaker-volume operations and extends its existing stats snapshot with voice-only fields. A Qt-free interval policy classifies those fields, while `RtcDemoController` publishes sanitized state to an isolated QML voice panel.

**Tech Stack:** C++20, locked libwebrtc, Qt 6/QML, Python 3, CMake/Ninja, CTest, Go.

## Global Constraints

- Work only in the ignored `codex/voice-control-quality` worktree.
- Keep movie audio, host voice, viewer voice, and future system audio as independent paths.
- Preserve existing microphone AEC, noise suppression, and automatic gain control; do not add unmeasured DSP tuning.
- Keep screen resolution, cadence, bitrate, codecs, queues, capture recovery, lifecycle recovery, signaling, and room behavior unchanged.
- Audio-focused automation must not launch MotionFixture or enable native speaker playout.
- Do not persist audio samples, device names, room identifiers, network addresses, or raw process output.
- Preserve repository-external libwebrtc checkouts and caches read-only.

---

### Task 1: Interval voice-quality policy

**Files:**
- Create: `client/tools/rtc_demo/voice_quality_policy.hpp`
- Create: `client/tools/rtc_demo/voice_quality_policy.cpp`
- Create: `tests/rtc/voice_quality_policy_test.cpp`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `VoiceQualitySnapshot` with cumulative packets received/lost, concealed samples, total received samples, and current jitter milliseconds.
- Produces: `VoiceQualityResult evaluate(snapshot, remote_muted)` returning `checking`, `good`, `unstable`, `poor`, or `muted` plus interval ratios.
- Produces: `reset()` for call shutdown.

- [x] Write RED tests for first snapshot, good/unstable/poor thresholds, missing fields, counter regression, remote mute, zero interval, and reset.
- [x] Build the focused target and verify failure because the policy is absent.
- [x] Implement consecutive-snapshot deltas with inclusive threshold boundaries and fail-closed regression handling.
- [x] Run the focused test, portable boundary scan, and `git diff --check`.
- [x] Commit `feat: classify primary voice quality`.

### Task 2: Native speaker volume and voice statistics

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`
- Modify: `tests/rtc/audio_device_factory_test.cpp`

**Interfaces:**
- Produces: `set_speaker_volume(int percent)`, `speaker_volume()`, and `speaker_volume_available()` on `SignaledPeer`.
- Extends: `SignaledMediaStats` with `local_audio_level`, `voice_packets_lost`, `voice_jitter_ms`, `voice_concealed_samples`, and `voice_total_samples_received`.
- Consumes: the same primary `AudioDeviceModule` already passed into `WebRtcRuntime`.

- [ ] Add RED tests for clamping/rejection, native min/max mapping, failed native set preserving the accepted value, no-playout unavailability, and extraction of only the expected voice RTP track.
- [ ] Run focused tests and verify the new API/assertions fail for the intended reason.
- [ ] Retain one scoped audio-device reference in `SignaledPeer::Impl`, map 0-100 using native min/max, and apply changes without recreating the runtime or track.
- [ ] Collect the five voice-only stats fields in the existing one-second snapshot and leave movie/video selection unchanged.
- [ ] Run `signaled_peer_test`, `audio_device_factory_test`, and the repeated lifecycle target.
- [ ] Commit `feat: expose primary voice device controls`.

### Task 3: Controller state and QML voice panel

**Files:**
- Modify: `client/tools/rtc_demo/call_session.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/shareme_app_controller.cpp`
- Modify: `client/tools/rtc_demo/qml/CallDetailsDrawer.qml`
- Modify: `tests/rtc/shareme_app_controller_test.cpp`
- Modify: `tests/scripts/gui_qml_contract_test.py`

**Interfaces:**
- Produces QML properties: `speakerVolume`, `speakerVolumeAvailable`, `microphoneLevel`, `voiceQuality`, and `voiceQualityMessage`.
- Produces: `setSpeakerVolume(int percent)` returning true only when the peer accepts the native device operation.
- Consumes: Task 1 policy and Task 2 peer snapshot/control API.

- [ ] Add RED controller tests proving accepted volume, rejected volume, mute independence, muted microphone level, conservative initial quality, interval transition, and shutdown reset.
- [ ] Add RED QML contract assertions for a microphone activity meter, speaker volume slider, processing summary, and quality text in a dedicated primary-voice section.
- [ ] Run focused C++ and Python tests and verify the new assertions fail.
- [ ] Publish state from the existing one-second worker onto the Qt thread without adding a second poller.
- [ ] Implement the voice panel and keep movie-audio diagnostics under the existing advanced section.
- [ ] Run controller, QML, CLI, and signaled-peer focused suites.
- [ ] Commit `feat: add primary voice quality controls`.

### Task 4: Fixture-free audio acceptance and stage evidence

**Files:**
- Create: `scripts/run_primary_voice_smoke.py`
- Create: `tests/scripts/primary_voice_smoke_test.py`
- Modify: `tests/scripts/CMakeLists.txt`
- Create: `docs/verification/primary-voice-control-quality.md`
- Modify: `docs/development/current-stage.md`
- Modify: this plan checklist.
- Generated ignored: `build/voice-control-dev/`, `out/primary-voice-control-quality/`.

**Interfaces:**
- Produces a bounded host/viewer synthetic primary-voice run with native playout disabled and no MotionFixture argument, process, or artifact path.
- Produces sanitized JSONL with executable hash, lifecycle booleans, primary-voice counter deltas, quality availability, control acknowledgements, and explicit `motion_fixture_started=false`.

- [ ] Add RED Python tests for command isolation, no native playout, no fixture launch, early exit, missing voice counters, failed control acknowledgements, redaction, and cleanup.
- [ ] Run the Python test and verify failure because the runner is absent.
- [ ] Implement the bounded runner by reusing parsing utilities without importing or starting screen-motion fixture ownership.
- [ ] Fresh-configure/build on macOS; run full CTest, `signaled_peer` 20 times, affected Python suites under both interpreters, Go race/vet, workflow 8/8, skill validator, portable-core scan, and `git diff --check`.
- [ ] Run one native two-peer synthetic primary-voice acceptance with speaker playout disabled; require bidirectional counter progress and explicit fixture absence.
- [ ] Request independent read-only review of audio ownership, thread safety, stats identity, failure truthfulness, QML behavior, privacy, default/Windows isolation, and evidence labels; fix every Critical/Important finding.
- [ ] Record human audible two-device acceptance and system audio as environment-dependent/unimplemented unless performed, update canonical handoff, and commit `docs: verify primary voice controls`.
- [ ] Push the feature branch; fast-forward `main` and clean only this worktree/local branch after every automatic gate passes and the remote SHA is verified.
