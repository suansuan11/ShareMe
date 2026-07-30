# WebRTC Signaled Test Call Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish two independently running native WebRTC test peers through the existing Qt and Go signaling service.

**Architecture:** Extract a one-peer controller from the tested loopback components. Qt forwards its opaque SDP and ICE JSON; the command-line probe reports only metrics and exits nonzero when media is absent.

**Tech Stack:** C++20, libwebrtc, Qt WebSockets, Go signaling service, CMake/CTest.

---

### Task 1: Native peer controller

**Files:** create `client/rtc/webrtc/src/signaled_peer.hpp`, `client/rtc/webrtc/src/signaled_peer.cpp`, `tests/rtc/signaled_peer_test.cpp`; modify WebRTC CMake files.

- [ ] Write a failing test for rejecting an invalid remote description and staging candidates before its remote description.
- [ ] Implement one PeerConnection with test video, synthetic audio, offer/answer callbacks, opaque SDP/ICE callbacks, bounded candidate staging, and connection/media metrics.
- [ ] Run WebRTC focused tests and commit `feat: add signaled WebRTC peer`.

### Task 2: Qt bridge and dual-process probe

**Files:** modify `client/signaling/qt_signaling_client.*`, `client/signaling/signaling_probe_main.cpp`, and signaling/WebRTC CMake files.

- [ ] Extend the Qt adapter with relay callbacks and attach it to `SignaledPeer` only when both optional dependencies are enabled.
- [ ] Add probe call mode: host stays alive after room creation, viewer joins, both forward SDP/ICE, and both print sanitized metrics only after media arrives.
- [ ] Start local Go service and run host/viewer call probes; assert selected candidate, received test video, and bidirectional audio counters.
- [ ] Commit `feat: connect WebRTC peers through signaling`.

### Task 3: Evidence and integration

**Files:** modify verification and architecture documents.

- [ ] Record exact commands/results and unverified boundaries.
- [ ] Run Go race/vet, default CTest, WebRTC tests, and Qt call probe; inspect staged sources only.
- [ ] Push, merge to main, and repeat applicable merged-main verification.
