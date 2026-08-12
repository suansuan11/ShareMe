# macOS Screen Motion Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove on macOS that an active high-quality screen-sharing call resumes video within five seconds after its owned dynamic screen content pauses and resumes, while voice remains continuous.

**Architecture:** Extend only the existing Python smoke orchestrator. Use macOS `SIGSTOP`/`SIGCONT` to control its owned fixture, preserve the complete product media path and current gates, and add phase-aware counter validation plus sanitized JSONL evidence.

**Tech Stack:** Python 3 standard library and `unittest`, CMake/Ninja, C++20/Qt 6, ScreenCaptureKit, locked libwebrtc, VideoToolbox, Go, CTest.

## Global Constraints

- Work only in the ignored `codex/macos-motion-recovery` worktree.
- Do not modify product capture, encoding, transport, decode, presentation, dimensions, cadence, bitrate, queues, or quality thresholds.
- The probe is macOS-only and optional; default and Windows behavior remain unchanged.
- Require at least ten measured seconds after resume and recovery within five counter samples.
- Never record fixture paths, PIDs, room identifiers, child output, or user data.
- Always resume a stopped fixture before cleanup.
- Preserve the external libwebrtc checkout and caches read-only.

---

### Task 1: Define configuration and process-control contracts

**Files:**
- Modify: `tests/scripts/screen_stream_smoke_test.py`
- Modify: `scripts/run_screen_stream_smoke.py`

**Interfaces:**
- Produces: immutable `MotionInterruption(after_seconds, duration_seconds)`.
- Produces: `validate_motion_interruption(config, duration_seconds, motion_fixture)`.
- Produces: macOS-only suspend/resume helpers.

- [ ] Write tests that reject a partial option pair, absent fixture, non-macOS platform, `after_seconds < 5`, duration outside `1..5`, and fewer than ten seconds after resume.
- [ ] Run the focused tests and confirm RED because these interfaces do not exist.
- [ ] Implement the minimal immutable configuration and validation functions.
- [ ] Write tests that `SIGSTOP` and `SIGCONT` target only the owned fixture PID and translate signal errors to sanitized `SmokeRuntimeError` categories.
- [ ] Implement the signal helpers using `getattr(signal, ...)` after platform validation.
- [ ] Run the focused suite and confirm GREEN.
- [ ] Commit `test: define macOS motion recovery probe`.

### Task 2: Add recovery evidence and gates

**Files:**
- Modify: `tests/scripts/screen_stream_smoke_test.py`
- Modify: `scripts/run_screen_stream_smoke.py`

**Interfaces:**
- Produces: `validate_motion_recovery(host_records, viewer_records, resume_sample, deadline_samples=5)`.
- Extends: `validate_records(..., motion_resume_sample: int | None = None)`.
- Produces: sanitized `motion_recovery` summary.

- [ ] Add synthetic counter tests that pass when every video key advances on sample five and voice advances by the end.
- [ ] Add rejection tests for missing warmup, late or partial video recovery, voice not advancing, regression, and less than ten post-resume samples.
- [ ] Run the focused tests and confirm RED.
- [ ] Implement shared role recovery validation without changing `_validate_continuous_progress`.
- [ ] Integrate the optional result into `validate_records` and confirm all previous callers produce the unchanged schema.
- [ ] Run the complete screen smoke suite and confirm GREEN.
- [ ] Commit `test: gate screen motion recovery`.

### Task 3: Orchestrate the live interruption safely

**Files:**
- Modify: `tests/scripts/screen_stream_smoke_test.py`
- Modify: `scripts/run_screen_stream_smoke.py`

**Interfaces:**
- Extends: `run_smoke(..., motion_interruption: MotionInterruption | None = None)`.
- Extends: CLI with the paired interruption options.
- Produces: run configuration, `motion-interruption` phase records, and summary lifecycle booleans/elapsed seconds.

- [ ] Write an orchestration test with a fake fixture and clock proving suspend then resume ordering, phase evidence, and CLI forwarding.
- [ ] Write a failure cleanup test proving `SIGCONT` happens before process-group termination when the probe is interrupted.
- [ ] Run focused tests and confirm RED.
- [ ] Schedule the two signal transitions inside the existing measured loop and store only integer elapsed seconds and phase names.
- [ ] In `finally`, conditionally resume before terminating the owned fixture; keep normal fixture and Windows flows unchanged.
- [ ] Forward paired CLI values as `MotionInterruption` and validate before artifact creation.
- [ ] Run affected Python suites under `/usr/bin/python3` and available Homebrew Python.
- [ ] Commit `test: exercise live screen motion recovery`.

### Task 4: Verify the native macOS stage and integrate

**Files:**
- Create: `docs/verification/macos-motion-recovery.md`
- Modify: `docs/development/current-stage.md`
- Modify: this plan checklist.
- Generated ignored: `build/call-dev/`, `out/macos-motion-recovery/`.

**Interfaces:**
- Produces: exact native test evidence, stage classification, and next handoff.

- [ ] Configure/build against the preserved external WebRTC archive and run full CTest plus `signaled_peer` 20 times.
- [ ] Run a 60-second native standard-profile smoke with the owned fixture, interruption at 15 seconds for three seconds, and a new artifact.
- [ ] Require H.264 VideoToolbox, matching geometry, bounded queues, nonzero bitrate, full video/voice continuity, one presentation recovery, and the new five-sample recovery gate.
- [ ] Run affected Python suites, Go race/vet, workflow 8/8, skill validator, portable-core scan, and `git diff --check`.
- [ ] Review all branch changes for lifecycle safety, schema compatibility, redaction, unsupported platform claims, generated files, and cache modifications.
- [ ] Record exact hashes/counters/results and keep actual sleep, minimize, occlusion, Windows, physical display/audio/temperature, and 4K explicitly unverified.
- [ ] Commit `docs: record macOS motion recovery`.
- [ ] If every frozen gate passes, push the branch, fast-forward `main`, rerun merged affected tests, push `main`, verify remote SHA, and remove only the merged worktree/local branch.
