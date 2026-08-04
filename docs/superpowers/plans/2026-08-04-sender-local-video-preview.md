# Sender Local Video Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the host's transmitted video track in the sender window while retaining remote rendering for viewers.

**Architecture:** Add a separately owned local-track sink to `SignaledPeer` and expose it through a local-frame callback. Route exactly one callback into the existing Qt delivery path according to role.

**Tech Stack:** C++20, libwebrtc, Qt 6 Multimedia, CMake/CTest, Python contract tests.

## Global Constraints

- Preserve independent movie-audio and voice paths.
- Preserve the external libwebrtc cache read-only.
- Use one writer and do not apply the archived debug stash.

---

### Task 1: Define local-preview contract

**Files:**
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`

- [ ] Add assertions that `SignaledPeerConfig` exposes `local_video_frame` and that the controller selects local frames for hosts and remote frames for viewers.
- [ ] Run the focused contract test and verify it fails because the callback and routing are absent.
- [ ] Add the local callback field without changing remote callback semantics.

### Task 2: Attach and clean up the local-track sink

**Files:**
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`

- [ ] Construct a distinct local sink from `local_video_frame`.
- [ ] Attach it to `video_track_` after track creation.
- [ ] Clear both callbacks and remove both sinks before track release in `stop()`.
- [ ] Run the focused contract and native RTC tests.

### Task 3: Route Qt rendering by role

**Files:**
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`

- [ ] Configure `local_video_frame` only for hosts and `remote_video_frame` only for viewers.
- [ ] Reuse the existing bounded conversion and queued `QVideoSink` delivery.
- [ ] Run the CLI contract, affected native tests, and full CTest suite.

### Task 4: Verify and hand off

**Files:**
- Create: `docs/verification/sender-local-video-preview.md`
- Modify: `docs/development/current-stage.md`

- [ ] Run a real-media macOS host/viewer smoke with the supplied movie.
- [ ] Record exact verified, partial, and environment-dependent evidence.
- [ ] Check staged scope and create focused commits.
