# macOS Evidence Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make ShareMe performance comparisons reject discontinuous or reused evidence and close the shared macOS regression boundary after the Windows H.264 merge.

**Architecture:** Extend the existing screen-smoke JSONL with a random non-sensitive run identity, then validate sample cadence, artifact independence, run identity, and executable SHA before aggregation. Keep native Windows codec code unchanged and prove the shared tooling and Apple media path on macOS.

**Tech Stack:** Python 3 standard library, unittest, C++20, CMake/Ninja, Qt 6, locked libwebrtc, Go, CTest.

## Global Constraints

- Work only on `codex/macos-evidence-hardening` in the owned ignored worktree.
- Do not modify Windows Media Foundation implementation files.
- Preserve external libwebrtc checkout, build, and cache trees read-only.
- Do not lower any media, cadence, queue, performance, or quality threshold.
- Generated JSONL, builds, logs, screenshots, caches, and machine identifiers stay ignored.
- macOS evidence never verifies Windows-native behavior.

---

### Task 1: Reject discontinuous process evidence

**Files:**
- Modify: `tests/scripts/windows_screen_acceptance_test.py`
- Modify: `scripts/run_windows_screen_acceptance.py`

**Interfaces:**
- Produces: `measurement_window(samples, start_s=30, end_s=150)` with exact one-second cadence validation.
- Produces: stable failures `measurement-window-incomplete`, `measurement-samples-invalid`, and `measurement-samples-discontinuous`.

- [ ] **Step 1: Write RED tests**

Add tests that reject a missing second, a duplicate elapsed second, non-integer elapsed time, negative/NaN CPU, and non-positive RSS. Retain the valid 30-through-150 sample case.

- [ ] **Step 2: Verify RED**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.scripts.windows_screen_acceptance_test.WindowsScreenAcceptanceTest.test_measurement_window_rejects_discontinuous_samples
```

Expected: FAIL because the current function accepts missing or duplicated intermediate samples.

- [ ] **Step 3: Implement the exact contract**

Validate ordered integer timestamps against `list(range(start_s, end_s + 1))`, require finite nonnegative numeric CPU and positive integer RSS, then return the selected window.

- [ ] **Step 4: Verify GREEN**

Run the complete `windows_screen_acceptance_test.py` suite and expect zero failures.

- [ ] **Step 5: Commit**

```bash
git add scripts/run_windows_screen_acceptance.py \
  tests/scripts/windows_screen_acceptance_test.py
git diff --cached --check
git commit -m "test: require continuous performance samples"
```

---

### Task 2: Require six independent runs and strict binary identity

**Files:**
- Modify: `scripts/run_screen_stream_smoke.py`
- Modify: `scripts/run_windows_screen_acceptance.py`
- Modify: `tests/scripts/screen_stream_smoke_test.py`
- Modify: `tests/scripts/windows_screen_acceptance_test.py`

**Interfaces:**
- Produces: a lowercase 32-hex `run_id` in every new `kind=run` JSONL record.
- Produces: `validate_distinct_artifacts(paths)` for six unique resolved paths.
- Produces: `summarize_run(...)["runId"]` and strict 64-lowercase-hex `demoSha256`.
- Produces: comparison failure categories `artifact-reused`, `run-identity-missing`, `run-identity-reused`, and `binary-identity-missing`.

- [ ] **Step 1: Write RED JSONL identity test**

Patch UUID generation to a deterministic value and assert `run_smoke` writes the `run_id` into the first record even when a later injected startup step fails.

- [ ] **Step 2: Verify RED**

Run the focused screen-smoke test and expect a missing `run_id` assertion failure.

- [ ] **Step 3: Emit the run identity**

Use `uuid.uuid4().hex`; do not derive identity from machine, path, user, room, time, or process ID.

- [ ] **Step 4: Write RED comparison tests**

Require failure when:

- one artifact path appears twice;
- two different paths contain the same run ID;
- a run ID is missing or malformed;
- a SHA contains non-hex or uppercase characters;
- the six valid run IDs differ while the executable SHA remains identical.

The last case must pass; run independence does not require different binaries.

- [ ] **Step 5: Verify RED**

Run `windows_screen_acceptance_test.py` and expect the duplicate-path/run-ID and malformed-SHA cases to fail.

- [ ] **Step 6: Implement strict identities**

Validate resolved paths before reading, validate `run_id` with `[0-9a-f]{32}`, validate SHA-256 with `[0-9a-f]{64}`, include `runId` in summaries, and require six unique run IDs inside `compare_standard`.

- [ ] **Step 7: Verify GREEN and affected contracts**

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests/scripts/screen_stream_smoke_test.py \
  tests/scripts/windows_screen_acceptance_test.py
```

- [ ] **Step 8: Commit**

```bash
git add scripts/run_screen_stream_smoke.py \
  scripts/run_windows_screen_acceptance.py \
  tests/scripts/screen_stream_smoke_test.py \
  tests/scripts/windows_screen_acceptance_test.py
git diff --cached --check
git commit -m "test: require independent performance runs"
```

---

### Task 3: Verify the affected macOS application boundary

**Files:**
- No source changes expected.
- Generated ignored output: `build/call-dev/`, `build/movie-call-dev/`, and `out/macos-evidence-hardening/`.

**Interfaces:**
- Consumes: the repository-external locked WebRTC build read-only.
- Produces: exact macOS build/test counts and native smoke results without changing Windows claims.

- [ ] **Step 1: Run all portable script contracts**

Use CTest so CLI contracts receive their configured executable/source paths. Run workflow tests and the ShareMe validator separately.

- [ ] **Step 2: Configure and build `call-dev`**

Reuse the existing `WEBRTC_ROOT` from the current local build configuration; do not download or rebuild external WebRTC.

- [ ] **Step 3: Run full `call-dev` CTest**

Record the exact current count. Repeat `signaled_peer` twenty times.

- [ ] **Step 4: Configure, build, and test `movie-call-dev`**

Use installed Qt/FFmpeg and the same locked WebRTC root. Record exact count or a precise dependency failure.

- [ ] **Step 5: Run a native standard screen smoke**

Run the local signaling service with a new ignored artifact and require H.264, `VideoToolbox`, active hardware encoding, matching host/viewer geometry, bounded queues, video/voice progress, bitrate, and one presentation recovery.

- [ ] **Step 6: Run GUI lifecycle smoke**

Use the configured app and record whether macOS permissions/display availability allow the six-state lifecycle.

- [ ] **Step 7: Run server and repository gates**

Run `go test -race ./...`, `go vet ./...`, workflow 8/8, skill validation, portable-core forbidden-header scan, and `git diff --check`.

---

### Task 4: Record and deliver the stage

**Files:**
- Create: `docs/verification/macos-evidence-hardening.md`
- Modify: `docs/development/current-stage.md`

**Interfaces:**
- Produces: one truthful outcome and the Windows rerun boundary.

- [ ] **Step 1: Write verification evidence**

Record exact commands, platform, build/test counts, smoke duration, codec diagnostics, and every environment-dependent check. Do not copy prior counts.

- [ ] **Step 2: Update canonical handoff**

State that new Windows formal comparisons must use independent run IDs and continuous samples. Preserve the historical Windows result while marking a rerun under the hardened runner as environment-dependent.

- [ ] **Step 3: Run final review gates**

Inspect the full branch diff, scan for generated/sensitive files, run all affected tests again, and verify the external WebRTC checkout remains unchanged.

- [ ] **Step 4: Commit documentation**

```bash
git add docs/verification/macos-evidence-hardening.md \
  docs/development/current-stage.md
git diff --cached --check
git commit -m "docs: record macOS evidence hardening"
```

- [ ] **Step 5: Prepare integration decision**

Verify branch cleanliness, commit topology, exact diff statistics, and remote state. Integration is permitted only when portable evidence validation passes and no shared macOS regression is introduced.
