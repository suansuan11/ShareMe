# Receiver Playout Reports Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Report the movie PTS submitted to the viewer's Qt video sink to the host with seek-generation isolation.

**Architecture:** Publish a same-generation movie-PTS/RTP anchor, reconcile rendered RTP timestamps in a pure helper, and send validated reports over the existing ordered control channel. The host observes but does not apply synchronization decisions.

**Tech Stack:** C++20, Qt 6 JSON/QML/Multimedia, libwebrtc RTP timestamps, CMake/CTest.

## Global Constraints

- Keep movie audio, voice, and video lifecycle independent.
- Do not change correction thresholds, playback rate, queue capacity, or hard-resync behavior.
- Preserve the external libwebrtc cache read-only.

---

### Task 1: Same-generation video anchor

**Files:**
- Modify: `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp`
- Modify: `client/rtc/movie/src/movie_video_source.cpp`
- Modify: `tests/rtc/movie_video_source_test.cpp`

- [x] Test that emitted samples include PTS, RTP timestamp, and generation and switch generation after seek.
- [x] Verify RED, implement an atomic snapshot, then verify GREEN.

### Task 2: Protocol and reconciliation

**Files:**
- Create: `client/tools/rtc_demo/playout_report.hpp`
- Create: `client/tools/rtc_demo/playout_report.cpp`
- Modify: `client/tools/rtc_demo/playback_state.*`
- Modify: `tests/rtc/playback_state_test.cpp`
- Create: `tests/rtc/playout_report_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

- [x] Test strict report validation, stale generation/sequence rejection, RTP wrap, and ten-second bounds.
- [x] Verify RED, implement the pure protocol helpers, then verify GREEN.

### Task 3: Viewer reporting and host observation

**Files:**
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

- [x] Test role-specific report publication and host observation UI contracts.
- [x] Verify RED, add the 250 ms viewer timer and sink-submission sampling, then expose host delta/action read-only.
- [x] Run focused and full native suites.

### Task 4: Verification and handoff

**Files:**
- Modify: `docs/protocols.md`
- Create: `docs/verification/receiver-playout-reports.md`
- Modify: `docs/development/current-stage.md`

- [x] Run real-media macOS smoke, Go tests, workflow tests, skill validation, and Git scope checks.
- [x] Record evidence boundaries and create focused commits.
