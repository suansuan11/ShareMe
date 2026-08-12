# macOS Screen Capture Restart Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Verify that a live macOS screen call recreates ScreenCaptureKit once and resumes high-quality video within five samples while bidirectional voice stays continuous.

**Architecture:** Keep one ref-counted `ScreenVideoSource` attached to the existing WebRTC track, stop/start its native backend through a bounded test probe, and combine exact restart-generation counters with role-aligned runner recovery gates.

**Tech Stack:** C++20, Qt 6, ScreenCaptureKit, VideoToolbox, locked libwebrtc, Python 3 `unittest`, CMake/Ninja, CTest, Go.

## Global Constraints

- Work only in the ignored `codex/macos-motion-recovery` worktree.
- Do not rebuild the WebRTC session or change quality, codec, timing, or queue contracts.
- Probe configuration is optional and macOS-only; default and Windows behavior stay unchanged.
- Exactly one restart attempt, success, and generation are required.
- Video recovery deadline is five samples; post-recovery observation is at least ten samples.
- Never record fixture paths, PIDs, room identifiers, child output, SDP, or ICE.
- Preserve the external libwebrtc checkout and caches read-only.

---

### Task 1: Define bounded orchestration contracts

**Files:** `scripts/run_screen_stream_smoke.py`, `tests/scripts/screen_stream_smoke_test.py`

- [x] Add RED tests for macOS-only paired timing, fixture ownership, warmup, bounded duration, and post-window.
- [x] Implement the immutable probe configuration and validation.
- [x] Add RED tests for exact `SIGSTOP`/`SIGCONT`, sanitized errors, and resume-before-cleanup.
- [x] Implement owned-fixture control and role-aligned phase records.
- [x] Verify the complete runner suite and commit the configuration/orchestration slices.

### Task 2: Define strict recovery evidence

**Files:** `scripts/run_screen_stream_smoke.py`, `tests/scripts/screen_stream_smoke_test.py`

- [x] Add RED synthetic tests for five-sample video recovery and continuous probe-window voice.
- [x] Reject late/partial recovery, missing counters, invalid boundaries, and insufficient post-window.
- [x] Add and parse exact restart attempt/success/generation counters.
- [x] Require a `0/0/0` to `1/1/1` host transition inside the probe window.
- [x] Preserve every existing full-call quality and continuity gate.

### Task 3: Recreate native capture without replacing the call

**Files:** `client/tools/rtc_demo/rtc_demo_controller.cpp`, `client/tools/rtc_demo/rtc_demo_controller.hpp`, `tests/rtc/screen_video_source_test.cpp`

- [x] Characterize stop/start/frame delivery on one `ScreenVideoSource` instance.
- [x] Retain that same source in the host controller and peer factory.
- [x] Use a runner-owned private trigger at the fixture boundary and perform
  exactly one macOS-only source stop/start.
- [x] Emit sanitized monotonic restart counters and a failure category.
- [x] Enable the native probe from the runner and verify focused C++/Python tests.
- [x] Commit the live native capture restart slice.

### Task 4: Native acceptance and integration

**Files:** `docs/verification/macos-motion-recovery.md`, `docs/development/current-stage.md`, this plan.

- [x] Fresh-configure/build `call-dev`; run CTest 51/51 and `signaled_peer` 20/20.
- [x] Preserve two initial 60-second fixture-only results as diagnostics, not acceptance.
- [x] Run a 60-second native restart gate at 15 seconds with a three-second fixture pause.
- [x] Re-run the final native gate after the strengthened generation-boundary validator.
- [x] Run affected Python suites, full CTest, Go race/vet, workflow 8/8, skill, portable-core, redaction, cache, and Git gates.
- [x] Review the complete branch and fix all Critical or Important findings.
- [x] Record exact hashes/counters/results and explicit environment boundaries.
- [x] Commit the verification documentation and canonical handoff.
- [ ] Push, fast-forward `main`, rerun merged gates, push `main`, verify remote SHA, and clean only the merged worktree/local branch.
