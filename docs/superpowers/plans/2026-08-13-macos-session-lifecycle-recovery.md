# macOS Session Lifecycle Recovery Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Observe macOS sleep/wake and lock/unlock, suspend conflicting capture recovery, and make one bounded post-resume decision without rebuilding a healthy call.

**Architecture:** A Qt-free policy folds nested lifecycle notifications into one generation. A macOS Objective-C++ adapter translates native notifications into typed events, while `RtcDemoController` owns a 750 ms evaluation timer and selects healthy return, existing capture recovery, or retryable connection-loss failure.

**Tech Stack:** C++20, Objective-C++, AppKit/Foundation, Qt 6, ScreenCaptureKit, locked libwebrtc, QML, Python 3, CMake/Ninja, CTest, Go.

## Global Constraints

- Work in the ignored `codex/macos-session-lifecycle-recovery` worktree.
- Keep signaling, PeerConnection, tracks, VideoToolbox, voice, dimensions, cadence, bitrate, queues, cursor, adaptation, presentation recovery, and retry delays unchanged.
- Do not implement automatic signaling reconnect/rejoin in this stage.
- Native notification observation and diagnostic injection are Apple-only; default and Windows behavior remain unchanged.
- Never programmatically sleep or lock the Mac from the unattended runner.
- Never serialize usernames, paths, PIDs, rooms, server addresses, notification payloads, tokens, SDP, ICE, or raw child output.
- Preserve repository-external libwebrtc checkouts and caches read-only.

---

### Task 1: Deterministic nested lifecycle policy

**Files:**
- Create: `client/tools/rtc_demo/session_lifecycle_policy.hpp`
- Create: `client/tools/rtc_demo/session_lifecycle_policy.cpp`
- Create: `tests/rtc/session_lifecycle_policy_test.cpp`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `SessionLifecycleEvent { will_sleep, did_wake, screen_locked, screen_unlocked }`.
- Produces: `SessionLifecycleState { inactive, suspended, evaluating, recovered, failed }`.
- Produces: `observe(event)`, `begin_evaluation()`, `record_recovered()`, `record_failed()`, `reset()`, `generation()`, `sleeping()`, and `locked()`.

- [x] Write literal RED tests for duplicate sleep/lock, nested wake-before-unlock, unlock-before-wake, stale resume, one evaluation per generation, terminal result, and reset.
- [x] Build the focused test and require compilation failure because the policy is absent.
- [x] Implement the minimal platform-free state machine; generation increments only on transition from fully active to suspended.
- [x] Run focused and portable-core scans, then commit `feat: define macOS session lifecycle policy`.

### Task 2: Native macOS lifecycle observer

**Files:**
- Create: `client/tools/rtc_demo/session_lifecycle_monitor.hpp`
- Create: `client/tools/rtc_demo/session_lifecycle_monitor.cpp`
- Create: `client/tools/rtc_demo/session_lifecycle_monitor_mac.mm`
- Create: `tests/rtc/session_lifecycle_monitor_test.cpp`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `SessionLifecycleMonitor::start(Callback)`, `stop()`, and default no-op platform implementation.
- Consumes: Task 1 typed events.
- macOS maps workspace sleep/wake and distributed session lock/unlock notifications to the four exact event values.

- [x] Write RED tests for start idempotence, stop callback rejection, event translation seam, and no-op unsupported behavior.
- [x] Verify focused compile/test failure before implementation.
- [x] Implement observer token ownership, queued callback delivery, and deterministic unregister on stop/destruction; do not expose native notification objects.
- [x] Build Objective-C++ and focused tests with warnings enabled, then commit `feat: observe macOS session lifecycle`.

### Task 3: Controller decision, UI, and controlled probe

**Files:**
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/qml/CallTopBar.qml`
- Modify: `client/tools/rtc_demo/qml/VideoStage.qml`
- Modify: `client/tools/rtc_demo/shareme_app_controller.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `tests/scripts/gui_qml_contract_test.py`
- Modify: `tests/rtc/shareme_app_controller_test.cpp`

**Interfaces:**
- Consumes: Tasks 1-2.
- Produces: `handleSessionLifecycleEvent(...)` and `evaluateSessionResume()`.
- Produces: one single-shot 750 ms settle timer and statuses `session-suspended:sleep`, `session-suspended:locked`, `session-resuming`, and `call-error: session-resume-connection-lost`.

- [x] Add RED controller/QML tests for nested event ordering, timer ownership, cancellation of capture retry timers, healthy return, recoverable-source routing, connection-loss result, Apple-only probe, and shutdown cleanup.
- [x] Run focused suites and verify the new assertions fail.
- [x] Implement monitor lifecycle and queued controller handling; suspend timers without stopping peer/tracks/voice.
- [x] Implement the 750 ms evaluation: connection unavailable fails, recoverable source reuses `beginScreenCaptureRecovery`, healthy source returns connected; restart monitoring only after a healthy result.
- [x] Add friendly Chinese suspended/resuming copy while preserving video and call controls.
- [x] Add a macOS-only opt-in typed-event trigger directory for controlled tests; it may create event files but may not post native OS notifications or invoke sleep/lock APIs.
- [x] Run focused CTest and Python suites, then commit `feat: coordinate macOS session resume`.

### Task 4: Safe lifecycle acceptance harness

**Files:**
- Create: `scripts/run_macos_session_lifecycle_smoke.py`
- Create: `tests/scripts/macos_session_lifecycle_smoke_test.py`
- Modify: `tests/scripts/CMakeLists.txt`

**Interfaces:**
- Produces controlled mode for four typed events and physical-wait mode for real native notifications.
- Produces sanitized JSONL lifecycle phase, generation, peer-liveness, media counter, and completion records.

- [x] Write RED runner tests for ordered controlled events, nested lock/sleep, timeout, early peer exit, missing native acknowledgement, media regression, redaction, and cleanup.
- [x] Verify RED because the runner is absent.
- [x] Implement controlled mode by writing only the Apple trigger files owned by Task 3.
- [x] Implement physical-wait mode that prints an instruction and waits; forbid `pmset`, AppleScript, CGSession, password prompts, or automatic display operations.
- [x] Require VideoToolbox H.264, geometry, process liveness, bounded resume result, video and voice progress, and ten post-resume samples.
- [x] Run both Python interpreters and commit `test: gate macOS session lifecycle recovery`.

### Task 5: Native controlled evidence, review, and integration

**Files:**
- Create: `docs/verification/macos-session-lifecycle-recovery.md`
- Modify: `docs/development/current-stage.md`
- Modify: this plan checklist.
- Generated ignored: `build/call-dev/`, `out/macos-session-lifecycle-recovery/`.

**Interfaces:**
- Produces exact controlled evidence and a physical-run handoff without overclaiming.

- [x] Fresh-configure/build `call-dev`; run full CTest, `signaled_peer` 20 times, affected Python suites under both interpreters, Go race/vet, workflow 8/8, skill validator, portable-core scan, redaction, cache status, and `git diff --check`.
- [x] Run one 60-second controlled nested lock/sleep/unlock/wake call and one controlled lock/unlock call with a post-event capture fault; require H.264 VideoToolbox, matching geometry, no unnecessary capture restart on healthy return, bounded authorized recovery on the fault path, video/voice progress, and ten post-resume samples.
- [x] Run independent read-only review for native observer ownership, queued-callback races, shutdown, nested policy, controller classification, default/Windows isolation, runner causality, and evidence labels; fix all Critical/Important findings.
- [x] Record physical sleep/wake and lock/unlock as environment-dependent unless the real native notifications are performed in this session.
- [x] Commit evidence, push feature branch, fast-forward `main` only after all automatic gates pass, verify merged tests and remote SHA, then remove only this worktree/local branch.
