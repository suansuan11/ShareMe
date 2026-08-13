# macOS Physical Sleep Continuity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the screen smoke gate exclude only causally proven lifecycle suspension samples while preserving all pre-resume and post-resume media quality checks.

**Architecture:** The lifecycle observer derives inclusive per-role counter ranges from typed generation-1 events and unique recovery markers. The base continuity validator skips those ranges and starts a fresh comparison segment afterward; lifecycle-specific validation continues to enforce five-sample recovery and ten post-resume samples.

**Tech Stack:** Python 3, `unittest`, JSONL screen-smoke artifacts, macOS AppKit lifecycle notifications, CMake/CTest.

## Global Constraints

- Do not change production media, controller, ScreenCaptureKit, WebRTC, signaling, voice, codec, quality, geometry, cadence, bitrate, queue, or recovery behavior.
- Do not increase the global five-sample stall threshold.
- Never infer an exclusion from wall-clock time or macOS power logs.
- Fail closed on malformed ranges, missing/duplicate recovery markers, or lifecycle generation mismatch.
- Preserve external libwebrtc checkouts and caches read-only.
- Keep generated JSONL and build output ignored.

---

### Task 1: Causal continuity exclusions

**Files:**
- Modify: `scripts/run_screen_stream_smoke.py`
- Modify: `scripts/run_macos_session_lifecycle_smoke.py`
- Test: `tests/scripts/screen_stream_smoke_test.py`
- Test: `tests/scripts/macos_session_lifecycle_smoke_test.py`

**Interfaces:**
- Produces: `_validate_continuous_progress(records, role, video_keys, *, excluded_ranges=()) -> dict`.
- Produces: `validate_records(..., continuity_exclusions=None) -> dict`.
- Produces: `LifecycleScenario.continuity_exclusions(host_reader, viewer_reader) -> dict[str, list[tuple[int, int]]]`.
- Consumes: existing `PERF_COUNTERS`, generation-1 lifecycle event lines, and the unique same-generation `session-lifecycle-recovered` marker.

- [x] **Step 1: Write the failing base-validator tests**

Add literal records where `encoded`, `callback`, and `submitted` are unchanged
for samples 2 through 9, recover at sample 10, and continue advancing. Assert
that `excluded_ranges=((2, 9),)` is accepted with eight excluded samples, while
the same stall without an exclusion and a six-sample stall after the exclusion
both raise `stalled for too long`.

- [x] **Step 2: Run RED for the base-validator tests**

Run:

```sh
python3 -m unittest \
  tests.scripts.screen_stream_smoke_test.ScreenStreamSmokeTest.test_continuity_excludes_only_explicit_lifecycle_range
```

Expected: failure because `_validate_continuous_progress` does not accept
`excluded_ranges`.

- [x] **Step 3: Implement minimal range validation and segmented continuity**

Normalize inclusive integer ranges, reject negative/reversed/overlapping ranges
and endpoints outside the role record list, filter excluded indices, and set the
previous counter to `None` after every excluded segment. Return the total
`excluded_samples`; keep all existing readiness, availability, monotonicity, and
five-stall checks for included records.

- [x] **Step 4: Run GREEN for base-validator tests**

Run the focused test from Step 2 and the full
`tests.scripts.screen_stream_smoke_test` module. Expected: all pass.

- [x] **Step 5: Write failing lifecycle-causality tests**

Build readers with literal interleaved counter/event lines. Assert the returned
host/viewer range starts at the counter count before `screen-locked` and ends at
the count before each role's generation-1 recovery marker. Assert missing
`will-sleep`, duplicate recovery markers, and generation 2 return no accepted
range by raising a lifecycle smoke error.

- [x] **Step 6: Run RED for lifecycle-causality tests**

Run:

```sh
python3 -m unittest \
  tests.scripts.macos_session_lifecycle_smoke_test.MacosSessionLifecycleSmokeTest.test_continuity_exclusions_require_complete_same_generation_causality
```

Expected: failure because `LifecycleScenario.continuity_exclusions` is absent.

- [x] **Step 7: Implement lifecycle range derivation and runner wiring**

Count role-aligned `PERF_COUNTERS` preceding the first required event and the
unique recovery marker. Validate the complete generation-1 sequence before
returning ranges. In `run_smoke`, request ranges from the scenario observer only
after readers are joined, then pass them to `validate_records`; ordinary screen
smokes continue with no exclusions.

- [x] **Step 8: Run GREEN and commit Task 1**

Run both script test modules with Homebrew and system Python, then commit only
the four Task 1 files with:

```sh
git commit -m "fix: exclude causal session suspension samples"
```

- [x] **Step 9: Close the physical restart-decision false rejection**

The first post-continuity physical rerun exposed that physical mode still
expected zero capture restarts when no controlled fault option was set. Write a
RED test with a unique generation-1 host `capture-restarted` marker and exact
`1/1/1` counters, then derive the physical zero/one expectation from that
marker. Keep controlled host decisions fixed and viewer decisions healthy. Run
both script modules under both Python interpreters and commit:

```sh
git commit -m "fix: accept physical capture recovery decision"
```

- [x] **Step 10: Bind restart counters to the causal lifecycle boundary**

Independent review found that a final `1/1/1` plus marker could still describe
a pre-sleep restart. Add RED cases for a restart before the suspension boundary,
a split counter transition, and a healthy marker with nonzero counters. Require
`0/0/0` through suspension, one synchronous `1/1/1` transition inside the
exclusion and no later than recovery, then stable `1/1/1` to the end.

---

### Task 2: Physical evidence and handoff

**Files:**
- Modify: `docs/verification/macos-session-lifecycle-recovery.md`
- Modify: `docs/development/current-stage.md`
- Modify: this plan checklist.
- Generated ignored: `out/macos-session-lifecycle-recovery/physical-clamshell-sleep-wake-final-180s.jsonl`.

**Interfaces:**
- Produces: one accepted fresh physical clamshell artifact and an explicit record of the two failed attempts.

- [x] **Step 1: Run automatic gates**

Run both affected Python modules under Homebrew and system Python, full CTest,
`signaled_peer` 20 times, Go race/vet, workflow 8/8, skill validation,
portable-core scan, artifact redaction, external cache status, and
`git diff --check`.

- [x] **Step 2: Run attended physical clamshell acceptance**

Start `physical-wait`, lock the screen, close the lid, wait at least ten seconds,
open and unlock, then retain at least ten post-resume samples. Require real
`pmset` Sleep/Wake evidence, the exact lifecycle sequence, H.264 VideoToolbox,
matching geometry, one bounded host capture restart, five-sample video/voice
recovery, and no included-range continuity violation.

- [x] **Step 3: Record evidence boundaries**

Document the successful artifact SHA-256 and counters. Record the shortcut
attempt as invalid because no system Sleep/Wake occurred, and record the first
clamshell artifact as a valid bug reproduction but failed acceptance. Keep
audible audio, scanout, thermals, other sleep modes, Windows, and reconnect/rejoin
outside the verified claim.

- [x] **Step 4: Review, commit, integrate, and clean**

Fix all Critical/Important review findings, commit focused evidence, push the
feature branch, fast-forward `main` only after merged tests pass, push `main`,
verify the remote SHA, preserve only valid/diagnostic ignored evidence, and
remove only this worktree and local feature branch.
