# Drift Study and Bounded Hard Resync Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `shareme-sol-terra`, `superpowers:test-driven-development`, `superpowers:using-git-worktrees`, and `superpowers:verification-before-completion`. Use at most one implementation writer. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure viewer sink-submission timeline drift reproducibly and implement an opt-in bounded hard-resync only when the frozen measurement gate passes.

**Architecture:** Add portable sample aggregation and policy state machines in `client/core`, keep JSONL/CLI/QTimer integration in the Qt RTC demo, and retain RTP mapping in the existing adapter. Execute measurement before correction code; a failed gate terminates the implementation path without relaxing constraints.

**Tech Stack:** C++20, Qt 6 Core/QML/Multimedia, libwebrtc DataChannel, CMake/CTest, Python orchestration, Go signaling.

## Global constraints

- Start from current `main` and verify `docs/development/current-stage.md` against Git and source.
- Work in an ignored feature worktree on a `codex/` branch.
- Preserve the repository-external libwebrtc cache read-only.
- Keep `client/core` free of Qt, FFmpeg, libwebrtc, platform headers, and filesystem I/O.
- Keep movie audio, host voice, viewer voice, and movie video independent.
- Do not change existing `SyncController` thresholds, queue capacities, drop/reject policy, report interval, codec policy, or performance targets.
- Do not commit raw JSONL, media, logs, build output, caches, local paths, secrets, IDE state, or unrelated changes.
- Treat Qt video-sink submission as application-level evidence, not display scanout.
- Record macOS and Windows evidence separately.

---

### Task 1: Establish the isolated execution baseline

**Files:**
- Read: `.agents/skills/shareme-sol-terra/SKILL.md`
- Read: `.agents/skills/shareme-sol-terra/references/project-contract.md`
- Read: `docs/development/current-stage.md`
- Read: `docs/superpowers/specs/2026-08-04-drift-study-bounded-hard-resync-design.md`
- Read: `docs/verification/receiver-playout-reports.md`

**Acceptance:** Clean isolated branch, current source matches the handoff, external cache location is identified without mutation, and baseline tests are not attributed to the new work.

- [ ] Check `git status --short --branch`, `git worktree list`, current `main`, remote relation, and named stash without modifying unrelated worktrees.
- [ ] Create an ignored worktree such as `.worktrees/drift-hard-resync` from current `main` on `codex/drift-hard-resync`.
- [ ] Configure `movie-call-dev` using the preserved external `WEBRTC_ROOT` and run the existing 40-test baseline.
- [ ] Run Go race/vet, workflow 8/8, and the ShareMe skill validator.
- [ ] If baseline fails, diagnose only the failure attributable to the current environment; do not begin feature work on an unexplained failing baseline.

### Task 2: Add portable drift sample and aggregation contracts

**Files:**
- Create: `client/core/include/shareme/core/drift_metrics.hpp`
- Create: `client/core/src/drift_metrics.cpp`
- Modify: `client/core/CMakeLists.txt`
- Create: `tests/core/drift_metrics_test.cpp`
- Modify: `tests/core/CMakeLists.txt`

**Interfaces:**
- Produce `enum class DriftPhase { warmup, steady, paused, post_resume, post_forward_seek, post_backward_seek, cooldown };`
- Produce `struct DriftSample` with monotonic capture time, sample index, report sequence, generation, host/viewer PTS, signed delta, buffer, phase, and observed `SyncAction`.
- Produce `DriftAggregator::accept(DriftSample)`, `complete_run()`, and `summary()` returning deterministic counts, percentiles, gaps, action counts, generation transitions, and recovery durations.

- [ ] Write RED tests for increasing indexes/sequences, current-generation samples, phase counts, signed min/max/mean, absolute P50/P95/P99/max, largest report gap, and recovery after three consecutive `abs(delta) <= 100` samples.
- [ ] Add RED boundary tests for empty input, overflow-safe absolute value, duplicate/regressing sequence, generation changes, paused gaps, percentile nearest-rank selection, and a recovery that never completes.
- [ ] Run only `drift_metrics` and verify failure is caused by the missing production API.
- [ ] Implement the minimum Qt-free data model and deterministic aggregation.
- [ ] Run `drift_metrics`, existing `sync_controller`, and portable-core tests; refactor only while green.
- [ ] Commit the portable aggregation as one focused commit.

### Task 3: Add opt-in sanitized JSONL capture

**Files:**
- Create: `client/tools/rtc_demo/drift_metrics_jsonl.hpp`
- Create: `client/tools/rtc_demo/drift_metrics_jsonl.cpp`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Create: `tests/rtc/drift_metrics_jsonl_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produce host-only `--metrics-jsonl PATH`, default disabled.
- Produce `DriftMetricsJsonlWriter::append(const DriftSample&)` and `finalize(const DriftSummary&)` using a temporary sibling file plus atomic rename.
- Output contains no room ID, media path, token, SDP, candidate address, ICE credential, or wall-clock timestamp.

- [ ] Write RED codec/writer tests for exact schema, one JSON object per line, monotonic sample indexes, final summary, path failure, and sanitized output.
- [ ] Add CLI RED tests rejecting viewer use, empty paths, output paths equal to the movie, and use without the movie source.
- [ ] Verify RED, then implement opt-in capture with a stable sanitized failure category.
- [ ] Wire host accepted reports into `DriftAggregator`; capture host timeline PTS and scenario phase on the Qt thread.
- [ ] Batch at most 64 encoded samples and flush on batch-full or once per second; reject and count any sample beyond the 64-entry pending bound. Finalize on normal stop. A write failure disables capture and surfaces status without ending the call.
- [ ] Run focused tests and `git diff --check`; commit the capture path separately.

### Task 4: Implement the deterministic five-minute playback scenario

**Files:**
- Create: `client/tools/rtc_demo/drift_scenario.hpp`
- Create: `client/tools/rtc_demo/drift_scenario.cpp`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Create: `tests/rtc/drift_scenario_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produce host-only `--drift-scenario drift-study-v1` and `--measurement-duration-seconds 300`; no other duration is accepted for v1.
- Produce a pure phase/action scheduler driven by elapsed monotonic milliseconds.
- Controller executes pause/resume/seek only through `MovieTimeline` and records every accepted/rejected action.

- [ ] Write RED tests for exact 30/90/95/150/210/300-second boundaries, a five-second pause, +60-second seek, -30-second seek, and one generation increment per accepted seek.
- [ ] Cover short movies, end-bound forward seeks, start-bound backward seeks, duplicate timer delivery, reconnect/call failure, and stop before completion.
- [ ] Reject the v1 scenario before start when fewer than 330 seconds remain on the movie timeline.
- [ ] Verify RED, implement the pure scheduler, then integrate it through one Qt timer without adding media-thread callbacks.
- [ ] Expose scenario phase and completion/failure in the demo UI and sanitized stdout result line; never print the movie path.
- [ ] Run focused tests and full CTest; commit the scenario separately.

### Task 5: Add a reproducible drift-study runner and summarizer

**Files:**
- Create: `scripts/run_movie_drift_study.py`
- Create: `tests/scripts/movie_drift_study_test.py`
- Modify: `tests/scripts/CMakeLists.txt`

**Interfaces:**
- Runner starts or attaches to local signaling, launches one host and one viewer, discovers the room from sanitized stdout, enforces timeouts, collects exit state, hashes artifacts, and invokes exactly three sequential five-minute runs.
- Arguments include explicit demo path, server URL, movie path, output root, and run count fixed to three for the acceptance profile.
- Summary contains platform/tool/build metadata, per-run and combined aggregates, gate results, and SHA-256 values; raw JSONL remains ignored.

- [ ] Write RED orchestration tests with fake subprocesses for room discovery, viewer launch, timeout, early exit, signal cleanup, artifact hashing, and path redaction.
- [ ] Add RED tests that partial runs never count as complete and cannot pass the gate.
- [ ] Verify RED and implement process cleanup without broad kill commands or destructive directory cleanup.
- [ ] Make the runner refuse an output root outside an explicit caller-provided directory and never delete pre-existing files.
- [ ] Run script tests and a short fake-process integration; commit the runner separately.

### Task 6: Execute and review the measurement gate

**Files:**
- Create after measurement: `docs/verification/movie-drift-study.md`
- Modify after measurement: `docs/performance-targets.md` only to add measured evidence references, not change targets.

**Required media:**
- `/Users/dio/Downloads/Media/紫罗兰的永恒花园TV（2018）内封简日字幕 4K（Ma10p x265 flac ass）/01.mkv`

- [ ] Build the exact current feature branch and record its commit, compiler, Qt, FFmpeg, libwebrtc manifest identity, macOS version, hardware, candidate type, and command without exposing cache/media paths in committed evidence.
- [ ] Run three sequential five-minute `drift-study-v1` sessions using the supplied movie; do not run them concurrently.
- [ ] Retain raw JSONL only below the ignored output root and compute SHA-256 for each artifact.
- [ ] Generate the combined summary and independently recompute sample counts and percentiles in the test runner.
- [ ] Check every measurement-gate condition from the design document explicitly.
- [ ] Write `movie-drift-study.md` with verified/partial/environment-dependent labels and exact reproducible commands using placeholders for machine-local roots.
- [ ] Ask one read-only reviewer to compare the summary against raw artifact hashes and gate logic.
- [ ] If any gate fails, commit the measurement implementation and failed evidence, update `current-stage.md` with automatic hard resync still unimplemented, and stop. Do not execute Tasks 7-10.
- [ ] If every gate passes, commit the verified evidence and proceed without changing the frozen candidate policy.

### Task 7: Define the sync-command codec and tracker with TDD

**Precondition:** Task 6 gate passed. Otherwise this task is forbidden.

**Files:**
- Create: `client/tools/rtc_demo/sync_command.hpp`
- Create: `client/tools/rtc_demo/sync_command.cpp`
- Create: `tests/rtc/sync_command_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`
- Modify: `docs/protocols.md`

**Interfaces:**
- Produce `SyncCommand`, strict encode/decode helpers, and `SyncCommandTracker`.
- Tracker accepts increasing sequence and generation exactly one greater than the last accepted playback generation; it never marks media reportable.

- [ ] Write RED round-trip and malformed-envelope tests covering room, version, action, sequence, JSON-safe bounds, target PTS, generation, old/future command, duplicate, and one-field mutation cases.
- [ ] Verify RED, implement the minimum codec/tracker, and verify GREEN.
- [ ] Update the protocol with the exact compatibility and failure behavior.
- [ ] Commit the codec separately.

### Task 8: Implement the portable bounded resync policy

**Precondition:** Task 6 gate passed.

**Files:**
- Create: `client/core/include/shareme/core/hard_resync_policy.hpp`
- Create: `client/core/src/hard_resync_policy.cpp`
- Modify: `client/core/CMakeLists.txt`
- Create: `tests/core/hard_resync_policy_test.cpp`
- Modify: `tests/core/CMakeLists.txt`

**Interfaces:**
- Inputs: monotonic report time, sequence, generation, host playback state, anchor availability, and `SyncDecision`.
- Outputs: `observe`, `trigger`, `cooldown`, `disabled_attempt_limit`, or `reset` plus attempt/episode counters.
- Constants are exactly four reports, at least 750 ms span, 10-second cooldown, and three attempts per call.

- [ ] Write RED tests for the fourth qualifying report, insufficient span, intervening non-hard decision, pause, generation change, stale sequence, missing anchor, cooldown boundaries, three-attempt disablement, and integer extremes.
- [ ] Prove the policy never mutates PTS, generation, queue, rate, or media state.
- [ ] Verify RED, implement minimal portable policy, run all core tests, and commit separately.

### Task 9: Wire opt-in bounded hard resync end to end

**Precondition:** Tasks 6-8 passed.

**Files:**
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `tests/rtc/playout_report_test.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produce host-only `--auto-hard-resync`, default false.
- Host trigger snapshots current timeline PTS, performs exactly one bounded `MovieTimeline::seek`, then sends one `sync-command` with the resulting generation.
- Viewer command acceptance invalidates rendered/report state and waits for the matching playback anchor.

- [ ] Add RED contract tests for default-off behavior, host-only flag validation, one seek/one generation increment/one command, viewer invalidation, cooldown status, and attempt-limit status.
- [ ] Add a bidirectional DataChannel test carrying the strict sync-command envelope.
- [ ] Verify RED, implement the smallest controller wiring, and keep all UI/control calls on the Qt thread.
- [ ] Never call video/audio source seek separately; never clear libwebrtc internals or add an unbounded queue.
- [ ] Expose current mode, episode count, attempt count, cooldown, last target, and disabled reason in host UI and metrics.
- [ ] Run focused tests, full CTest, and a 20-run peer lifecycle repetition; commit separately.

### Task 10: Close the stage with proportional verification

**Files:**
- Create: `docs/verification/bounded-hard-resync.md` only if Tasks 7-9 ran.
- Modify: `docs/development/current-stage.md`
- Modify: `docs/performance-targets.md` only with measured references.

- [ ] Run a fresh movie-call build and complete CTest suite.
- [ ] Run Go `test -race ./...` and `go vet ./...`.
- [ ] Run workflow 8/8, ShareMe skill validation, `git diff --check`, Git status, staged-scope review, and repository-external cache preservation checks.
- [ ] Rerun the supplied movie for the relevant real-media acceptance. If no natural bounded trigger occurs, report hard-resync GUI/media correction as partial; do not inject a production backdoor merely to force it.
- [ ] Separate macOS evidence from unperformed Windows evidence and separate sink submission from scanout/acoustic evidence.
- [ ] Request one final read-only review for specification compliance and Critical/Important issues; fix all valid findings and rerun affected verification.
- [ ] Create focused commits, update the dynamic handoff at the stage boundary, and stop on the feature branch unless merge/push authority is explicitly available in the executing task.

## Completion outcomes

The executing Agent must end in exactly one of these states:

1. **Measurement gate failed:** measurement tooling and honest evidence are committed; automatic hard resync remains unimplemented; failed conditions and next experiment are explicit.
2. **Measurement gate passed, bounded resync delivered:** measurement evidence, codec, portable policy, opt-in integration, verification, and handoff are committed with platform boundaries.
3. **Blocked environment:** implementation progress is committed only if coherent; missing platform/device/runtime and the exact unperformed evidence are explicit. A timeout, one failed run, or incomplete long test is not silently converted into success.
