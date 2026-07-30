# Qt Signaling Client Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a transport-free client signaling state machine and an optional Qt WebSocket adapter for the local Go service.

**Architecture:** `client/core/signaling` owns v1 envelopes and room state. Qt owns `QWebSocket`, translates text frames, and remains excluded from default builds.

**Tech Stack:** C++20, Qt 6 WebSockets, CMake/CTest, local Go signaling service.

---

### Task 1: Portable session

**Files:** create `client/core/include/shareme/signaling/signaling_session.hpp`, `client/core/src/signaling_session.cpp`, `tests/core/signaling_session_test.cpp`; modify `client/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`.

- [ ] Write a failing test where `create_room()` yields a `create-room` host envelope and a valid `room-created` response stores room ID and token.
- [ ] Run `cmake --preset dev && cmake --build --preset build-dev && ctest --test-dir build/dev -R signaling_session --output-on-failure`; expect a missing-session failure.
- [ ] Implement typed `Envelope`, `SignalingSession`, role/state accessors, monotonic sequence, create/join, server-event input, and opaque relay output. Reject invalid responses, relay-before-room, role mismatch, and non-retryable errors.
- [ ] Re-run focused and full default CTest; expect zero failures.
- [ ] Commit `feat: add portable signaling session`.

### Task 2: Qt boundary and probe

**Files:** create `client/app/signaling/qt_signaling_client.hpp`, `client/app/signaling/qt_signaling_client.cpp`, `client/tools/signaling_probe/main.cpp`, `client/tools/signaling_probe/CMakeLists.txt`; modify root and tools CMake files and presets.

- [ ] Add a host/viewer probe contract using `--server ws://127.0.0.1:8080/v1/ws`.
- [ ] Implement QWebSocket ownership, bearer reconnect, sanitized status signals, and core-only envelope serialization.
- [ ] Add `Qt6::WebSockets` only to the optional Qt graph; default CMake remains Qt-free.
- [ ] With Qt available, run local Go server then host/viewer probes; verify create/join and opaque relay. Otherwise record environment limitation without claiming native verification.
- [ ] Commit `feat: add Qt signaling adapter`.

### Task 3: Documentation and integration

**Files:** modify `README.md`, `docs/architecture.md`; create `docs/verification/qt-signaling-client.md`.

- [ ] Record startup, build, verification commands, and exclusions: no PeerConnection, media, TURN, or Windows capture.
- [ ] Run Go race/vet and default CTest; run Qt integration if dependencies exist.
- [ ] Verify no generated output is tracked, commit/push, merge to `main`, then re-run tests on merged main.

## Self-review

Task 1 covers the portable contract, Task 2 covers Qt transport, and Task 3 covers evidence. The scope intentionally stops before WebRTC media binding.
