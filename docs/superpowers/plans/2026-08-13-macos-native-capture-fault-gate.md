# macOS Native Capture Delegate Fault Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove on macOS that the actual ScreenCaptureKit delegate error entry triggers one bounded recovery and that a late retired-delegate error cannot poison the replacement stream.

**Architecture:** Two default-false diagnostic commands cross the existing `ScreenVideoSource` and macOS backend boundaries. `ScreenCaptureKitStream` invokes its real Objective-C delegate for the current and retained retired stream, while the smoke runner owns trigger ordering and accepts only one exact recovery followed by unchanged counters and continuing media.

**Tech Stack:** C++20, Objective-C++, ScreenCaptureKit, Qt 6, locked libwebrtc, Python 3 `unittest`, CMake/Ninja, CTest, Go.

## Global Constraints

- Work in the ignored `codex/macos-native-capture-fault-gate` worktree.
- Keep PeerConnection, tracks, VideoToolbox, voice, dimensions, cadence, bitrate, queues, cursor, adaptation, presentation recovery, and retry delays unchanged.
- Compile trigger parsing and native fault commands only on Apple platforms; default execution and Windows remain unchanged.
- The current fault must enter through `SCStreamDelegate::didStopWithError`; the controller must not call recovery directly for this gate.
- Never serialize trigger paths, pointers, raw NSError descriptions, rooms, SDP, ICE, tokens, PIDs, or child output.
- Preserve repository-external libwebrtc checkouts and caches read-only.

---

### Task 1: Screen adapter diagnostic command contract

**Files:**
- Modify: `client/rtc/screen/include/shareme/rtc/screen_video_source.hpp`
- Modify: `client/rtc/screen/src/screen_video_source.cpp`
- Modify: `client/rtc/screen/include/shareme/rtc/macos_screen_capture_source.hpp`
- Modify: `client/rtc/screen/src/macos_screen_capture_source.cpp`
- Modify: `tests/rtc/screen_video_source_test.cpp`
- Modify: `tests/rtc/macos_screen_capture_source_test.cpp`

**Interfaces:**
- Produces: `ScreenCaptureBackend::inject_current_stream_stop_for_diagnostics()` and `inject_retired_stream_stop_for_diagnostics()`, both defaulting to `false`.
- Produces: matching `ScreenVideoSource` forwarding methods.
- Produces: matching `MacScreenCaptureStream` and `MacScreenCaptureSource` methods.

- [x] **Step 1: Write failing forwarding tests**

Extend `FakeScreenCaptureBackend` and `FakeScreenCaptureStream` with explicit booleans and call counts. Assert literal false for an unsupported backend, then enable each fake command and require exactly one forwarded call and result.

```cpp
REQUIRE(!source->inject_current_stream_stop_for_diagnostics());
backend->current_fault_available = true;
REQUIRE(source->inject_current_stream_stop_for_diagnostics());
REQUIRE(backend->current_fault_calls == 2);
REQUIRE(source->inject_retired_stream_stop_for_diagnostics());
REQUIRE(backend->retired_fault_calls == 1);
```

- [x] **Step 2: Verify RED**

Run:

```bash
cmake --build build/call-dev --target shareme_screen_video_source_test shareme_macos_screen_capture_source_test
```

Expected: compilation fails because the diagnostic methods do not exist.

- [x] **Step 3: Add minimal forwarding methods**

Add default-false virtuals only at the adapter boundaries and direct forwarding in `ScreenVideoSource` and `MacScreenCaptureSource`. Do not add state, timers, environment parsing, or recovery behavior.

- [x] **Step 4: Verify GREEN and commit**

Run focused CTest for `screen_video_source|macos_screen_capture_source`; require both pass. Stage only the six adapter/test files and commit `feat: expose macOS capture fault diagnostics`.

### Task 2: Real delegate injection and retired-generation rejection

**Files:**
- Modify: `client/rtc/screen/src/macos_screen_capture_source.mm`
- Modify: `tests/rtc/macos_screen_capture_source_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 diagnostic methods.
- Produces: fixed sanitized current error `screen-capture-stopped-9001`.
- Produces: one retained retired delegate for the active diagnostic episode; it is released after stale injection or shutdown.

- [x] **Step 1: Write the failing native behavior test**

Register a macOS-only Objective-C++ test target that starts the real adapter against a narrow diagnostic stream seam, injects the current delegate fault, performs stop/start, injects the retired delegate fault, and asserts:

```cpp
REQUIRE(stream.inject_current_stream_stop_for_diagnostics());
REQUIRE(stream.error() == "screen-capture-stopped-9001");
stream.stop();
REQUIRE(stream.start(callback));
REQUIRE(stream.error().empty());
REQUIRE(stream.inject_retired_stream_stop_for_diagnostics());
REQUIRE(stream.error().empty());
REQUIRE(!stream.inject_retired_stream_stop_for_diagnostics());
```

The test seam must invoke the same `ShareMeScreenCaptureDelegate` methods and `handle_stream_error` path as production; a gate-only assertion is insufficient.

- [x] **Step 2: Verify RED**

Build and run the new macOS-only target. Expected: current/retired delegate commands are unavailable and the sanitized error is not produced.

- [x] **Step 3: Implement current and retired delegate commands**

Under the stream mutex, require an active delegate for current injection, retain it once as the retired diagnostic delegate, and invoke:

```objective-c
NSError *fault = [NSError errorWithDomain:@"ShareMeCaptureDiagnostics"
                                     code:9001
                                 userInfo:nil];
[delegate stream:stream didStopWithError:fault];
```

For stale injection, require a replacement active generation and a retained old delegate, invoke its same method with code 9002, then release diagnostic state. The old delegate block carries the retired generation and must be rejected by `handle_stream_error`. Clear retained diagnostic state on destructor, terminal startup failure, and explicit final cleanup without altering the real stream lifecycle.

- [x] **Step 4: Verify lifecycle and mutation cases**

Require focused tests to catch wrong error code, direct handler bypass, missing one-shot release, accepting the old generation, injection without an active stream, and retained state surviving shutdown.

- [x] **Step 5: Verify GREEN and commit**

Run the native diagnostic test plus `macos_screen_capture_source` and `screen_video_source`; require all pass. Commit `test: exercise native ScreenCaptureKit delegate faults`.

### Task 3: Controller and smoke-runner causal gate

**Files:**
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `scripts/run_screen_stream_smoke.py`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `tests/scripts/screen_stream_smoke_test.py`

**Interfaces:**
- Consumes: Task 1 commands and existing recovery counters.
- Produces: macOS-only current and retired trigger acknowledgements.
- Produces: sanitized artifact fields `native_delegate_fault_verified`, `retired_delegate_fault_rejected`, and `post_stale_samples`.

- [x] **Step 1: Write failing controller and runner tests**

Require the controller's current trigger to call only `inject_current_stream_stop_for_diagnostics()`, keep normal error polling active, and never call `beginScreenCaptureRecovery` from the trigger lambda. Require a separate stale trigger to call only `inject_retired_stream_stop_for_diagnostics()` after generation one. Require both paths inside `#if defined(__APPLE__)` and both timers stopped in `stopPeer()`.

Runner tests use fake readers and literal counter sequences to prove:

```python
0, 0, 0  # before current delegate fault
1, 1, 1  # recovery acknowledged before fixture resume
1, 1, 1  # unchanged for at least two samples after stale fault
```

They must fail separately for missing current injection acknowledgement, recovery timeout, stale injection timeout, second recovery, missing post-stale samples, and media/voice regression.

- [x] **Step 2: Verify RED**

Run `tests/scripts/rtc_demo_cli_test.py` and `tests/scripts/screen_stream_smoke_test.py`. Expected: trigger routing, stale state, and artifact assertions fail.

- [x] **Step 3: Implement controller trigger routing**

Parse two non-empty trigger paths only under `__APPLE__`. The current timer stops only after the native command returns true and prints `SMOKE_STATUS native-delegate-fault-injected`. The stale timer starts after successful recovery; on a runner-owned stale file it calls the retired command, prints `SMOKE_STATUS retired-delegate-fault-injected`, and stops. Neither lambda changes status or recovery policy directly.

- [x] **Step 4: Implement bounded runner sequencing**

Create both files inside one `TemporaryDirectory`. Suspend the motion fixture before touching the current trigger. Resume only after exact `1/1/1`. Wait for two later host/viewer samples, touch the stale trigger, require its acknowledgement within three seconds, then require unchanged restart fields and progressing video/voice through at least ten final samples. Cleanup resumes the fixture and terminates peers before deleting the temporary directory on every exit.

- [x] **Step 5: Verify GREEN and commit**

Run both Python suites with system and Homebrew Python, focused controller/source CTest, redaction tests, and `git diff --check`. Commit `test: gate native macOS capture fault recovery`.

### Task 4: Native acceptance, review, documentation, and integration

**Files:**
- Create: `docs/verification/macos-native-capture-fault-gate.md`
- Modify: `docs/development/current-stage.md`
- Modify: `docs/superpowers/plans/2026-08-13-macos-native-capture-fault-gate.md`
- Generated ignored: `build/call-dev/`, `out/macos-native-capture-fault-gate/`.

**Interfaces:**
- Produces: exact current-platform evidence and the canonical next-stage handoff.

- [x] **Step 1: Run fresh regression gates**

Fresh-configure and build `call-dev`; run full CTest, `signaled_peer` 20 times, affected Python suites under both interpreters, Go `test -race ./...` and vet, workflow 8/8, skill validation, portable-core forbidden-header scan, artifact redaction, external-cache status, and `git diff --check`.

- [x] **Step 2: Run the native controlled delegate gate**

Run one 60-second standard Cocoa smoke with the owned motion fixture, current delegate fault at 15 seconds, three-second fixture suspension, and stale delegate fault after two recovered samples. Require H.264 VideoToolbox, matching host/viewer geometry, exact `0/0/0 -> 1/1/1`, recovering status before restart success, unchanged counters after stale injection, host/viewer video recovery within five samples, bidirectional synthetic voice continuity, bounded presentation recovery, and at least ten post-stale samples.

- [x] **Step 3: Review and record evidence**

Review Objective-C ownership, generation races, shutdown cleanup, trigger causality, timeout bounds, Windows/default isolation, schema/redaction, and cache scope. Fix all Critical and Important findings and rerun affected gates. Record platform, binary/artifact hashes, exact counters, evidence labels, and the physical-fault limitation.

- [x] **Step 4: Commit and integrate**

Commit verification and handoff documentation. If every frozen gate passes, push the feature branch, fast-forward `main`, rerun merged affected tests, push `main`, verify the exact remote SHA, then remove only this merged worktree and local feature branch.
