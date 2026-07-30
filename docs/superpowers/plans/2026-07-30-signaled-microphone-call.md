# Signaled Microphone Call Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the existing two-process signaled call with real microphones and verify bidirectional microphone RTP and audio level.

**Architecture:** Add an audio mode to `SignaledPeerConfig`, route it through the existing audio-device factory, and keep Qt responsible only for CLI selection and signaling transport. Preserve synthetic mode as the deterministic regression path.

**Tech Stack:** C++20, libwebrtc native audio device/APM, Qt WebSockets, AVFoundation permission preflight, Go signaling service, Python smoke orchestration.

---

### Task 1: Configurable signaled audio source

**Files:** modify `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`, `client/rtc/webrtc/src/signaled_peer.cpp`, and `tests/rtc/signaled_peer_test.cpp`.

- [x] Add failing tests for synthetic/microphone config validation and mode-to-processing policy.
- [x] Extend `SignaledPeer::create` with `SignaledPeerConfig`; use microphone permission preflight, `AudioDeviceMode::microphone`, and `AudioSourceKind::microphone` only when explicitly selected.
- [x] Add local audio-level collection to `SignaledPeerResult`; preserve typed creation failure without fallback.
- [x] Run focused WebRTC tests and commit `feat: add microphone mode to signaled peer`.

### Task 2: CLI and dual-process microphone verification

**Files:** modify `client/tools/signaled_call/main.cpp`, `client/tools/signaled_call/CMakeLists.txt`, `scripts/run_signaled_call_smoke.py`; create `client/tools/signaled_call/Info.plist`.

- [x] Add `--audio synthetic|microphone`, reject other values, and pass the selected mode to the peer.
- [x] Embed macOS microphone usage metadata and include sanitized audio level in result output.
- [x] Extend the smoke script with `--audio`; require positive audio levels in microphone mode while retaining synthetic acceptance.
- [x] Run synthetic and microphone dual-process smoke tests; commit `feat: verify signaled microphone call`.

### Task 3: Documentation and integration

**Files:** modify `README.md`, `docs/architecture.md`; create `docs/verification/signaled-microphone-call.md`.

- [x] Record permission behavior, commands, metrics, exclusions, and exact verified environment.
- [ ] Run Go race/vet, default CTest, complete call CTest, synthetic smoke, and microphone smoke.
- [ ] Commit documentation, push, merge to `main`, repeat merged-main verification, and clean the feature worktree.
