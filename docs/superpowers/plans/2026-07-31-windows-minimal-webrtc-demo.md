# Windows Minimal WebRTC Demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing WebRTC loopback and two-process signaling call work on Windows, then render the receiver's synthetic remote video in a minimal Qt demo.

**Architecture:** Keep `WebRtcRuntime`, `LoopbackSignaling`, `SignaledPeer`, `QtSignalingClient`, and the Go relay as the single media/signaling path. Add only the missing Windows socket lifetime, a transport-neutral remote-frame callback, Windows-safe smoke process handling, and a thin Qt presentation adapter.

**Tech Stack:** C++20, locked libwebrtc revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`, Qt 6.8+, CMake/Ninja, Python 3, Go signaling service, Windows/MSVC.

## Global Constraints

- Do not download WebRTC or change the locked revision.
- Do not replace or redesign the existing WebRTC dependency bootstrap.
- Preserve the one-host/one-viewer protocol and existing SDP/ICE relay format.
- Keep Qt outside `client/core` and keep native WebRTC types outside QML.
- Use bounded waits and idempotent shutdown; release sinks and peer proxies before runtime threads.
- Do not claim Desktop Duplication, WASAPI, TURN, two-computer, or 1080p60 performance acceptance in this slice.

---

### Task 1: Preserve and Commit the Existing Windows Environment Changes

**Files:**
- Modify: `CMakePresets.json`
- Existing change: `scripts/bootstrap_webrtc.py`

**Interfaces:**
- Consumes: existing external WebRTC root `D:/Deps/shareme-webrtc`.
- Produces: `webrtc-dev-windows`, `build-webrtc-dev-windows`, and Windows batch-tool invocation used by the prepared environment.

- [ ] **Step 1: Normalize the preset indentation without changing values**

Replace the tab before `WEBRTC_ROOT` with spaces so `git diff --check` is clean.

- [ ] **Step 2: Run the bootstrap script contract test**

Run: `python tests/scripts/bootstrap_webrtc_test.py`

Expected: exit code 0 without downloading or invoking bootstrap commands.

- [ ] **Step 3: Confirm the prepared Windows WebRTC tree still configures**

Run: `cmake --preset webrtc-dev-windows`

Expected: configure succeeds and reports the existing locked WebRTC installation.

- [ ] **Step 4: Commit**

```powershell
git add CMakePresets.json scripts/bootstrap_webrtc.py
git commit -m "build: support prepared Windows WebRTC toolchain"
```

### Task 2: Initialize Winsock for the WebRTC Runtime

**Files:**
- Modify: `client/rtc/webrtc/src/webrtc_runtime.hpp`
- Modify: `client/rtc/webrtc/src/webrtc_runtime.cpp`
- Test: `tests/rtc/loopback_signaling_test.cpp`
- Test: `tests/rtc/webrtc_loopback_test.cpp`

**Interfaces:**
- Consumes: `webrtc::WinsockInitializer` from the locked libwebrtc Windows API.
- Produces: unchanged `WebRtcRuntime::create(...)`; runtime creation now guarantees socket initialization on Windows.

- [ ] **Step 1: Record the red regression evidence**

Run:

```powershell
ctest --test-dir build/webrtc-dev-windows -R "^(loopback_signaling|webrtc_loopback)$" --output-on-failure
```

Expected: both tests fail after approximately ten seconds with loopback negotiation timeout.

- [ ] **Step 2: Add a Windows-only Winsock owner to the runtime**

Forward declare or privately store a Windows-only RAII owner in
`WebRtcRuntime`. Construct it before `Thread::CreateWithSocketServer()`, reject
a nonzero `error()`, and release it only after the network thread is stopped.
Keep non-Windows source and ABI behavior unchanged.

- [ ] **Step 3: Build the affected targets**

Run: `cmake --build --preset build-webrtc-dev-windows --target shareme_loopback_signaling_test shareme_webrtc_loopback_test`

Expected: both targets link successfully with the existing WebRTC archive.

- [ ] **Step 4: Verify green**

Run:

```powershell
ctest --test-dir build/webrtc-dev-windows -R "^(loopback_signaling|webrtc_loopback)$" --output-on-failure
```

Expected: 2/2 tests pass, including received video/audio and host candidate evidence.

- [ ] **Step 5: Commit**

```powershell
git add client/rtc/webrtc/src/webrtc_runtime.hpp client/rtc/webrtc/src/webrtc_runtime.cpp
git commit -m "fix: initialize Winsock for WebRTC runtime"
```

### Task 3: Make the Signaled Call Smoke Runner Portable to Windows

**Files:**
- Modify: `tests/scripts/signaled_call_smoke_test.py`
- Modify: `scripts/run_signaled_call_smoke.py`

**Interfaces:**
- Consumes: Python `subprocess.Popen` and platform process APIs.
- Produces: `popen_group_options()` and `terminate_process_group()` behavior that is safe on POSIX and Windows.

- [ ] **Step 1: Add failing Windows-process contract tests**

Add tests that patch `os.name`/platform helpers and assert Windows launches use
`CREATE_NEW_PROCESS_GROUP`, do not call `os.getpgrp`/`os.killpg`, terminate a
live process, and close captured streams.

- [ ] **Step 2: Run the contract test and verify red**

Run: `python tests/scripts/signaled_call_smoke_test.py --script scripts/run_signaled_call_smoke.py`

Expected: failure because the Windows launch/cleanup helpers do not exist.

- [ ] **Step 3: Implement the minimum platform split**

Use `start_new_session=True` only on POSIX. On Windows use
`creationflags=subprocess.CREATE_NEW_PROCESS_GROUP`; terminate with bounded
`terminate()`/`wait()` followed by `kill()` if required. Preserve the POSIX
process-group safety checks.

- [ ] **Step 4: Verify green**

Run: `python tests/scripts/signaled_call_smoke_test.py --script scripts/run_signaled_call_smoke.py`

Expected: all smoke-runner contract tests pass.

- [ ] **Step 5: Commit**

```powershell
git add tests/scripts/signaled_call_smoke_test.py scripts/run_signaled_call_smoke.py
git commit -m "fix: support Windows signaled call smoke cleanup"
```

### Task 4: Expose Remote Video Frames from SignaledPeer

**Files:**
- Create: `client/rtc/webrtc/src/remote_video_sink.hpp`
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`

**Interfaces:**
- Consumes: `webrtc::VideoFrame` received by the remote video track.
- Produces: `using RemoteVideoFrameCallback = std::function<void(const webrtc::VideoFrame&)>;` and `SignaledPeerConfig::remote_video_frame`.

- [ ] **Step 1: Write a failing sink callback test**

Construct one I420 `webrtc::VideoFrame`, deliver it to the proposed sink, and
assert the callback observes the width, height, and timestamp while existing
frame counters also advance.

- [ ] **Step 2: Build/run and verify red**

Run: `cmake --build --preset build-webrtc-dev-windows --target shareme_signaled_peer_test`

Expected: compile failure because the remote callback/sink API is not defined.

- [ ] **Step 3: Implement the focused remote sink and lifecycle**

Add a sink that updates the current counting metrics and invokes the optional
callback synchronously. Replace the counting-only sink in `SignaledPeer`, and
disable callback delivery before removing the sink during `stop()`.

- [ ] **Step 4: Verify green and containing suite**

Run:

```powershell
cmake --build --preset build-webrtc-dev-windows --target shareme_signaled_peer_test
ctest --test-dir build/webrtc-dev-windows -R "^(signaled_peer|webrtc_loopback)$" --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 5: Commit**

```powershell
git add client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp client/rtc/webrtc/src/signaled_peer.cpp client/rtc/webrtc/src/remote_video_sink.hpp tests/rtc/signaled_peer_test.cpp
git commit -m "feat: expose remote WebRTC video frames"
```

### Task 5: Add the Minimal Qt Sender/Receiver Demo

**Files:**
- Create: `client/tools/rtc_demo/CMakeLists.txt`
- Create: `client/tools/rtc_demo/main.cpp`
- Create: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Create: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Create: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `client/tools/CMakeLists.txt`
- Modify: `docs/verification/webrtc-signaled-call.md`
- Test: `tests/scripts/signaled_call_cli_test.py`

**Interfaces:**
- Consumes: `QtSignalingClient`, `SignaledPeer`, and its remote-frame callback.
- Produces: `shareme_rtc_demo --server <ws-url> --role host|viewer [--room <id>]`.

- [ ] **Step 1: Extend the CLI contract test with the demo argument contract**

Assert missing server/role and viewer-without-room exit with code 2. Assert the
host and viewer help text documents the exact options.

- [ ] **Step 2: Configure/build and verify red**

Configure Qt + WebRTC with the installed Qt prefix and existing WebRTC root,
then build target `shareme_rtc_demo`.

Expected: target-not-found failure.

- [ ] **Step 3: Implement the controller and minimal view**

The controller owns `QtSignalingClient` and `SignaledPeer`, exposes room/status
properties, forwards SDP/ICE exactly as the existing probe does, converts I420
frames to a detached `QImage` off the UI object, and posts the image to a
`QVideoSink` using a guarded queued invocation. QML contains only status, room
code, and `VideoOutput`.

- [ ] **Step 4: Build and run automated verification**

Build the demo and existing signaled-call probe, then run the CLI contracts,
complete WebRTC CTest suite, Go tests, and the Windows two-process smoke command.

- [ ] **Step 5: Perform visual acceptance**

Start the local signaling service, launch a host demo and viewer demo with the
printed room code, and verify the viewer displays the changing test pattern.
Record this as manual Windows verification without claiming two-computer or
performance acceptance.

- [ ] **Step 6: Update run documentation and commit**

```powershell
git add client/tools/rtc_demo client/tools/CMakeLists.txt tests/scripts/signaled_call_cli_test.py docs/verification/webrtc-signaled-call.md
git commit -m "feat: add minimal Qt WebRTC sender receiver demo"
```

### Task 6: Final Verification, Integration, and Push

**Files:**
- Modify only if verification finds a scoped defect.

**Interfaces:**
- Consumes: all preceding commits.
- Produces: a tested merged `main` pushed to `origin`.

- [ ] **Step 1: Run fresh full verification**

Run the portable CTest suite, Windows WebRTC CTest suite, Qt/WebRTC build and
contracts, Go tests, smoke call, `git diff --check`, and `git status --short`.

- [ ] **Step 2: Commit any verification-only scoped fixes**

Use `fix:` or `test:` prefixes and rerun the affected plus full verification.

- [ ] **Step 3: Merge the feature branch into `main`**

Use a normal non-force merge after confirming `origin/main` has not diverged.

- [ ] **Step 4: Verify the merged tree and push**

Run the relevant full suites on merged `main`, then execute `git push origin main`.
