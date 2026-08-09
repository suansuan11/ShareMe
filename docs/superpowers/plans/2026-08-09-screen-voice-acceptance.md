# Screen Streaming Quality and Voice Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver real primary voice in interactive screen calls and an auditable macOS screen/video/voice/recovery acceptance runner without lowering video quality.

**Architecture:** Make recording source and native voice playout explicit peer configuration, extend the existing non-blocking stats snapshot with primary-voice RTP totals, and enforce continuity plus presentation recovery in the sanitized screen smoke runner. Keep Movie audio and external libwebrtc unchanged.

**Tech Stack:** C++20, Objective-C++/macOS, Qt 6/QML, WebRTC stats API, Python 3 unittest/JSONL, CMake/CTest, Go signaling tests.

## Global Constraints

- Preserve 1080p60, 1440p60, and 4K30 profile policies; do not lower quality.
- Do not modify the external libwebrtc checkout, build, or cache.
- Keep Movie audio, host voice, and viewer voice independent.
- Keep `client/core` free of Qt, FFmpeg, WebRTC, and platform headers.
- All queues remain bounded and observable.
- Automated smoke uses synthetic voice and disabled playout; interactive screen calls use microphone and native playout.
- Windows native media evidence remains unverified on macOS.
- No system audio, HDR, remote input, TURN, Movie Stage 2B, or file sharing.

---

### Task 1: Explicit primary voice runtime policy

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produces: `SignaledPeerConfig::native_audio_playout` and an RTC demo `--audio microphone|synthetic`, `--no-audio-playout` contract.

- [ ] Add failing peer tests proving `native_audio_playout=false` is accepted and invalid audio modes are rejected.
- [ ] Add failing CLI tests proving interactive defaults are microphone/playout and explicit synthetic/no-playout validation is accepted.
- [ ] Run focused tests and confirm the new assertions fail for missing policy wiring.
- [ ] Add `native_audio_playout` to `SignaledPeerConfig` and pass it to `PeerConnection::SetAudioPlayout`.
- [ ] Parse RTC demo voice options, pass `SignaledAudioMode` and playout policy into `RtcDemoController`, and then into `SignaledPeerConfig`.
- [ ] Keep automated commands explicit; never silently fall back from microphone to synthetic.
- [ ] Run focused C++ and CLI tests until green.
- [ ] Commit as `feat: enable primary voice in screen calls`.

### Task 2: Non-blocking primary voice statistics

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produces: `SignaledMediaStats media_stats() const noexcept` with video fields and `voice_packets_sent`, `voice_packets_received`, `voice_bytes_sent`, `voice_bytes_received`.
- Consumes: expected primary voice track helpers already used by final wait stats.

- [ ] Add failing tests for outbound/inbound primary voice selection and periodic media-stat fields.
- [ ] Confirm the focused tests fail before production changes.
- [ ] Rename the periodic result/API to `SignaledMediaStats` and collect expected audio RTP packet/byte totals in the existing async stats report.
- [ ] Update the controller worker and sanitized `PERF_COUNTERS` line without adding a second stats poller.
- [ ] Assert the Qt counter timer contains no direct `GetStats` or `media_stats()` call.
- [ ] Run signaled-peer and CLI contract suites until green.
- [ ] Commit as `feat: expose primary voice continuity stats`.

### Task 3: Geometry and voice continuity gates

**Files:**
- Modify: `scripts/run_screen_stream_smoke.py`
- Modify: `tests/scripts/screen_stream_smoke_test.py`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`

**Interfaces:**
- Produces: monotonic counter validation, five-sample maximum voice stall window, host/viewer geometry agreement, and JSONL summary fields.
- Consumes: Task 2 `PERF_COUNTERS` voice totals.

- [ ] Add failing Python tests for missing voice fields, counter regression, six-sample stalls, dimension disagreement, and valid five-sample DTX gaps.
- [ ] Run the focused Python suite and confirm expected failures.
- [ ] Add the four voice integer keys to the parser and require explicit synthetic/no-playout in host/viewer commands.
- [ ] Implement monotonic/progress validation after warm-up and profile/aspect geometry checks.
- [ ] Include voice packet totals and maximum no-progress windows in the sanitized summary.
- [ ] Run the focused Python suite and registered smoke contract until green.
- [ ] Commit as `test: gate screen calls on voice continuity`.

### Task 4: Bounded presentation recovery probe

**Files:**
- Modify: `client/tools/rtc_demo/video_preview_adapter.hpp`
- Modify: `client/tools/rtc_demo/video_preview_adapter.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/rtc/video_preview_adapter_test.cpp`
- Modify: `scripts/run_screen_stream_smoke.py`
- Modify: `tests/scripts/screen_stream_smoke_test.py`

**Interfaces:**
- Produces: `VideoPreviewAdapter::close_ingress()`, `reopen_ingress(QVideoSink*)`, `presentation_epoch`, and `presentation_recovery_count` diagnostics.

- [ ] Add failing adapter tests: close rejects frames, reopen accepts only a new frame, pending depth stays at one, and no stale pending frame is replayed.
- [ ] Confirm the adapter tests fail before implementation.
- [ ] Implement close/reopen on the shared state without recreating the adapter or peer.
- [ ] Under `SHAREME_SCREEN_RECOVERY_PROBE`, schedule one viewer close/reopen after initial submissions and record the first post-recovery submission.
- [ ] Extend counters and runner tests to require exactly one recovery and post-recovery progress when the probe is enabled.
- [ ] Run adapter, CLI, and smoke-contract suites until green.
- [ ] Commit as `feat: verify bounded screen presentation recovery`.

### Task 5: Native acceptance, review, and handoff

**Files:**
- Modify: `docs/verification/screen-streaming-quality-voice.md`
- Modify: `docs/development/current-stage.md`
- Modify: `docs/superpowers/plans/2026-08-09-screen-voice-acceptance.md`

**Interfaces:**
- Produces: exact macOS evidence, Windows/environment boundaries, rollback SHA, and next-stage recommendation.

- [ ] Fresh-configure and build `call-dev` and `movie-call-dev` against the preserved external WebRTC root.
- [ ] Run focused tests, full CTest, Go race/vet, workflow 8/8, skill validation, portable-header scan, and `git diff --check`.
- [ ] Run macOS native standard 10/30 seconds, quality 30 seconds, cinema 30 seconds, and one cinema 120-second stability/recovery gate on distinct ports.
- [ ] Run an interactive microphone/playout launch where current permissions permit; label actual listening and echo observations separately from RTP evidence.
- [ ] Review the full branch for lifecycle, stats-thread, audio-path isolation, privacy, and scope findings; resolve Critical/Important findings.
- [ ] Update verification and canonical handoff with verified/partial/environment-dependent/unimplemented labels.
- [ ] Commit as `docs: record screen and voice acceptance`.
- [ ] Push the feature branch and verify the exact remote ref.
- [ ] If all merge gates pass, fast-forward `main`, rerun full affected tests, push `main`, and remove only this merged worktree.

