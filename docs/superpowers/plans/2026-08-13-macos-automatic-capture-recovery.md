# macOS Automatic Screen Capture Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Automatically restart a failed macOS ScreenCaptureKit stream at most three times without rebuilding the active WebRTC call or interrupting voice.

**Architecture:** A Qt-free policy object produces deterministic 250/500/1000 ms retry decisions. `RtcDemoController` owns one retry timer, routes real late-source errors and the macOS-only acceptance probe through the same recovery entry point, restarts the retained `ScreenVideoSource`, and exposes recovering or exhausted status through the existing GUI contract.

**Tech Stack:** C++20, Qt 6, ScreenCaptureKit, locked libwebrtc, QML, Python 3 `unittest`, CMake/Ninja, CTest, Go.

## Global Constraints

- Work in the ignored `codex/macos-automatic-capture-recovery` worktree.
- Keep signaling, PeerConnection, video track, VideoToolbox selection, voice tracks, resolution, cadence, bitrate, queues, and quality thresholds unchanged.
- Recover only `screen-capture-stopped-*`; make at most three attempts after 250, 500, and 1000 ms.
- Default and Windows behavior remain unchanged; acceptance triggers are macOS-only and opt-in.
- Never expose paths, PIDs, rooms, SDP, ICE, tokens, raw child output, or unsanitized NSError content.
- Preserve repository-external libwebrtc checkouts and caches read-only.

---

### Task 1: Deterministic recovery policy

**Files:**
- Create: `client/tools/rtc_demo/screen_capture_recovery_policy.hpp`
- Create: `client/tools/rtc_demo/screen_capture_recovery_policy.cpp`
- Create: `tests/rtc/screen_capture_recovery_policy_test.cpp`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `enum class ScreenCaptureRecoveryState { inactive, waiting, attempting, recovered, exhausted }`.
- Produces: `ScreenCaptureRecoveryPolicy::begin()`, `begin_attempt()`, `record_success()`, `record_failure()`, `reset()`, `state()`, `attempt()`, and `delay_ms()`.
- Contract: delays are exactly `{250, 500, 1000}` milliseconds and no fourth attempt exists.

- [ ] **Step 1: Write the failing policy test**

Add a standalone C++ test that verifies initial inactive state, begin idempotence, attempt numbers 1/2/3, exact delays 250/500/1000, terminal success, terminal exhaustion, and reset to inactive.

- [ ] **Step 2: Run the focused test and verify RED**

Run `cmake --build build/call-dev --target shareme_screen_capture_recovery_policy_test && ctest --test-dir build/call-dev -R '^screen_capture_recovery_policy$' --output-on-failure`.
Expected: configure or compilation fails because the policy files and target do not exist.

- [ ] **Step 3: Implement the minimal state machine**

Use a fixed `std::array<int, 3>{250, 500, 1000}`. `begin()` succeeds only from inactive; `begin_attempt()` moves waiting to attempting; success is terminal; failure after attempts one and two returns to waiting, and failure after attempt three becomes exhausted. No timer, callback, Qt type, media object, or platform header belongs in this class.

- [ ] **Step 4: Register sources and test target, then verify GREEN**

Add the policy sources to `shareme_rtc_demo_sources`; compile the test from the same implementation file and run the focused CTest. Expected: one test passes.

- [ ] **Step 5: Commit**

Stage only the five policy/CMake files and commit `feat: define bounded capture recovery policy`.

### Task 2: Controller recovery orchestration

**Files:**
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `tests/rtc/screen_video_source_test.cpp`

**Interfaces:**
- Consumes: `ScreenCaptureRecoveryPolicy` from Task 1.
- Produces: `beginScreenCaptureRecovery(QString category)` and `runScreenCaptureRecoveryAttempt()` private controller functions.
- Produces: one owned `screen_capture_recovery_timer_` and existing monotonic attempt/success/generation counters.

- [ ] **Step 1: Write failing source and lifecycle contract tests**

Extend the Python controller contract to require recoverable-prefix filtering, one owned retry timer, calls to both recovery methods, `screen-capture-recovering:`, terminal `call-error: screen-capture-recovery-exhausted`, timer cancellation in `stopPeer()`, and `#if defined(__APPLE__)` around the trigger path. Extend the screen-source test to prove a failed restart can be attempted again on the same source and clears the previous runtime error after success.

- [ ] **Step 2: Run focused tests and verify RED**

Run the new RTC CLI test and `ctest --test-dir build/call-dev -R '^screen_video_source$' --output-on-failure`. Expected: recovery methods/timer are absent and the prescribed restart sequence is not yet implemented.

- [ ] **Step 3: Route late runtime errors into the policy**

Change `checkScreenCaptureError()` so only `screen-capture-stopped-*` begins automatic recovery. Other categories retain the sanitized terminal `screen-capture-error:<category>` status. Stop late-error polling during an active episode and ignore duplicate begin calls.

- [ ] **Step 4: Execute attempts on one owned timer**

Before each scheduled attempt publish `screen-capture-recovering:<attempt>`. On timeout increment attempts, call `screen_video_source_->stop()` and `start()`. Success increments successes and generation once, resets policy, sets `connected`, restarts late-error polling, and emits the smoke marker. Failure schedules the next policy delay or sets `call-error: screen-capture-recovery-exhausted` after attempt three.

- [ ] **Step 5: Preserve shutdown and probe causality**

Stop the retry timer and reset policy in `stopPeer()`. Change the existing macOS trigger poller to call `beginScreenCaptureRecovery("screen-capture-stopped-probe")` instead of directly restarting the source, so controlled acceptance and real late-error handling share the same entry point.

- [ ] **Step 6: Verify GREEN and commit**

Build the demo and focused C++ test, run the full RTC CLI contract, and commit the controller/source/test changes as `feat: recover failed macOS screen capture`.

### Task 3: Recovery UX and smoke evidence

**Files:**
- Modify: `client/tools/rtc_demo/qml/CallTopBar.qml`
- Modify: `client/tools/rtc_demo/qml/VideoStage.qml`
- Modify: `client/tools/rtc_demo/shareme_app_controller.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `tests/scripts/gui_qml_contract_test.py`
- Modify: `tests/rtc/shareme_app_controller_test.cpp`
- Modify: `scripts/run_screen_stream_smoke.py`
- Modify: `tests/scripts/screen_stream_smoke_test.py`

**Interfaces:**
- Consumes: controller statuses from Task 2.
- Produces: visible “正在恢复屏幕共享” state and friendly exhausted-recovery result.
- Produces: smoke validation that the trigger used the automatic policy and first-attempt recovery remained `0/0/0 -> 1/1/1`.

- [ ] **Step 1: Write failing QML and application-status tests**

Require top-bar and host-stage copy for statuses beginning with `screen-capture-recovering:` while keeping the video output and controls alive. Require `ShareMeAppController` to treat `call-error: screen-capture-recovery-exhausted` through the existing result-page flow and friendly screen-capture message.

- [ ] **Step 2: Run focused GUI tests and verify RED**

Run the GUI QML and app-controller focused tests. Expected: recovery copy/status routing assertions fail.

- [ ] **Step 3: Implement minimal visible states**

Map recovering status to “正在恢复屏幕共享” in `CallTopBar.qml`; keep the last video frame visible and show recovery copy on the host stage without changing viewer behavior. Reuse the existing screen/capture friendly error mapping for the exhausted category.

- [ ] **Step 4: Strengthen smoke classification**

Require the host output to contain the recovering status marker before the successful restart marker, preserve exact first-attempt counters, and classify missing policy entry separately from restart acknowledgement timeout. Keep the artifact schema sanitized and compatible.

- [ ] **Step 5: Verify GREEN and commit**

Run affected GUI, CLI, and screen-smoke suites under system and Homebrew Python, then commit `test: gate automatic capture recovery`.

### Task 4: Native acceptance, review, and integration

**Files:**
- Create: `docs/verification/macos-automatic-capture-recovery.md`
- Modify: `docs/development/current-stage.md`
- Modify: this plan checklist.
- Generated ignored: `build/call-dev/`, `out/macos-automatic-capture-recovery/`.

**Interfaces:**
- Produces: exact current-platform evidence and canonical next-stage handoff.

- [ ] **Step 1: Configure and run regression gates**

Fresh-configure/build `call-dev`; run full CTest, `signaled_peer` 20 consecutive times, affected Python suites under both available interpreters, Go `test -race ./...` and vet, workflow 8/8, skill validation, portable-core scan, redaction, cache-preservation, and `git diff --check`.

- [ ] **Step 2: Run native controlled recovery**

Run a new 60-second standard-profile Cocoa smoke with the owned motion fixture, failure trigger at 15 seconds, and three-second fixture pause. Require H.264 VideoToolbox, matching geometry, exact first-attempt `1/1/1`, host/viewer video recovery within five samples, continuous bidirectional voice, bounded viewer presentation recovery, and at least ten post-recovery samples.

- [ ] **Step 3: Review and record evidence**

Review lifecycle races, retry bounds, error classification, Windows/default isolation, QML behavior, schema/redaction, generated scope, and external cache. Fix all Critical/Important findings, rerun affected gates, then record exact platform, binary/artifact hashes, counters, and evidence boundaries.

- [ ] **Step 4: Commit and integrate**

Commit verification/handoff documentation. If every frozen gate passes, push the feature branch, fast-forward `main`, rerun merged affected tests, push `main`, verify the remote SHA, and remove only the merged worktree and local feature branch.
