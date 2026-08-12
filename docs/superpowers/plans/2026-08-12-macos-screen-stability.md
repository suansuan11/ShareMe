# macOS Screen Stability Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make long-run macOS screen smoke evidence own and monitor deterministic moving content so an unchanged desktop cannot be misclassified as a native encoder stall.

**Architecture:** Keep ScreenCaptureKit, VideoToolbox, WebRTC, and all continuity thresholds unchanged. Extend the existing Python smoke orchestrator with an optional owned motion-fixture process, include that process in the current guard checks, and record only sanitized lifecycle booleans in JSONL.

**Tech Stack:** Python 3 standard library, `unittest`, CMake/Ninja, Qt 6 QML fixture, C++20, locked libwebrtc, Go, CTest.

## Global Constraints

- Work only on `codex/macos-screen-stability` in the owned ignored worktree.
- Do not modify ScreenCaptureKit, VideoToolbox, Media Foundation, codec selection, dimensions, cadence, bitrate, queue limits, or quality thresholds.
- Keep existing Windows-owned fixture orchestration compatible.
- Generated builds, JSONL, logs, screenshots, process output, and caches remain ignored.
- Preserve the external WebRTC checkout, archives, depot tools, and caches read-only.
- macOS evidence never verifies Windows-native behavior.

---

### Task 1: Define owned fixture lifecycle

**Files:**
- Modify: `tests/scripts/screen_stream_smoke_test.py`
- Modify: `scripts/run_screen_stream_smoke.py`

**Interfaces:**
- Produces: `build_motion_fixture_command(fixture: Path, profile: str, duration_seconds: int) -> list[str]`.
- Produces: `start_motion_fixture(...) -> subprocess.Popen` using the existing process-group and Windows job safeguards.
- Produces: sanitized fixture request/start/alive/stopped evidence.

- [x] **Step 1: Write RED lifecycle tests**

Add focused tests that assert:

```python
command = runner.build_motion_fixture_command(
    Path("fixture"), "standard", 30
)
self.assertEqual(command, [
    "fixture", "--profile", "standard", "--duration-seconds", "60"
])
```

Patch process creation and assert output is suppressed, `QT_QPA_PLATFORM` is
removed from the fixture environment, the process-group options are used, and
Windows job attachment remains delegated through `_start_measured_demo`.

- [x] **Step 2: Verify RED**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 -m unittest \
  tests.scripts.screen_stream_smoke_test.ScreenStreamSmokeTest.test_motion_fixture_command_and_process_are_bounded -v
```

Expected: FAIL because the fixture helpers do not exist.

- [x] **Step 3: Implement fixture helpers**

The command duration is `duration_seconds + 30`, capped at 3600. Reject a
missing/non-file fixture before creating the JSONL artifact. Start with
`stdout=DEVNULL`, `stderr=DEVNULL`, the visible desktop environment, and
`popen_group_options()`.

- [x] **Step 4: Verify GREEN**

Run the complete `screen_stream_smoke_test.py` suite under system Python.

- [x] **Step 5: Commit**

```bash
git add scripts/run_screen_stream_smoke.py tests/scripts/screen_stream_smoke_test.py
git diff --cached --check
git commit -m "test: define owned screen motion fixture"
```

---

### Task 2: Guard the full smoke and record sanitized evidence

**Files:**
- Modify: `tests/scripts/screen_stream_smoke_test.py`
- Modify: `scripts/run_screen_stream_smoke.py`

**Interfaces:**
- Extends: `run_smoke(..., motion_fixture: Path | None = None) -> dict`.
- Extends: CLI with `--motion-fixture PATH`.
- Produces: run field `motion_fixture_requested`.
- Produces: summary fields `motion_fixture_started`,
  `motion_fixture_alive`, and `motion_fixture_stopped`.

- [x] **Step 1: Write RED orchestration tests**

Use fake processes and patched signaling startup to prove:

- a supplied fixture starts before signaling;
- an early or mid-run fixture exit fails as `motion-fixture-early-exit`;
- caller-supplied guards and the owned fixture are both checked;
- success and failure paths terminate the fixture exactly once;
- JSONL contains only booleans and never the fixture path, PID, or child output;
- no-fixture calls preserve the existing schema and behavior.

- [x] **Step 2: Verify RED**

Run the new focused tests and expect `run_smoke` to reject the unknown
`motion_fixture` argument or omit the expected lifecycle evidence.

- [x] **Step 3: Implement the minimal orchestration**

Start the fixture inside the existing artifact-owned lifecycle, add it to the
effective guard tuple, and refactor the success return so cleanup completes
before the final summary is written. On cleanup, call
`terminate_process_group(fixture, grace_seconds=1)` only if it is still alive.
Never add raw fixture diagnostics to success or failure records.

- [x] **Step 4: Expose the CLI**

Add:

```python
parser.add_argument("--motion-fixture", type=Path)
```

Resolve the supplied path and forward it to `run_smoke`. Preserve all existing
callers when the option is absent.

- [x] **Step 5: Verify affected Python contracts**

Run the five affected suites under `/usr/bin/python3` and Homebrew Python:

```bash
python3 -m unittest \
  tests.scripts.gui_call_smoke_test \
  tests.scripts.process_metrics_test \
  tests.scripts.screen_stream_smoke_test \
  tests.scripts.windows_gui_acceptance_test \
  tests.scripts.windows_screen_acceptance_test
```

Expected: all executable tests pass; only the configured Windows fixture test
may remain skipped on macOS.

- [x] **Step 6: Commit**

```bash
git add scripts/run_screen_stream_smoke.py tests/scripts/screen_stream_smoke_test.py
git diff --cached --check
git commit -m "test: own motion during screen stability smoke"
```

---

### Task 3: Native macOS stability acceptance

**Files:**
- Generated ignored: `build/call-dev/`
- Generated ignored: `out/macos-screen-stability/`

**Interfaces:**
- Consumes: the locked repository-external WebRTC archive read-only.
- Produces: 30-second and 120-second fixture-owned native JSONL evidence.

- [x] **Step 1: Fresh configure and build**

Configure `call-dev` with the existing external `WEBRTC_ROOT` and installed Qt
prefix, then build `shareme_rtc_demo` and `shareme_screen_motion_fixture`.

- [x] **Step 2: Run full CTest and repeated lifecycle**

Run the complete `call-dev` CTest suite and `signaled_peer` 20 consecutive
times. Record exact counts from current output.

- [x] **Step 3: Run 30-second native gate**

```bash
/usr/bin/python3 scripts/run_screen_stream_smoke.py \
  --demo build/call-dev/client/tools/rtc_demo/shareme_rtc_demo \
  --motion-fixture build/call-dev/client/tools/screen_motion_fixture/shareme_screen_motion_fixture \
  --server-root server --profile standard --duration-seconds 30 \
  --artifact out/macos-screen-stability/standard-30s.jsonl
```

Require H.264, VideoToolbox active, matching geometry, video/voice progress,
nonzero bitrate, bounded queues, zero continuity stalls, and one recovery.

- [x] **Step 4: Run 120-second native gate**

Repeat with a distinct port and artifact for 120 seconds. Apply the same gates;
missing or stalled evidence is failure.

- [x] **Step 5: Run repository gates**

Run Go race/vet, workflow 8/8, skill validation, portable-core forbidden-header
scan, and `git diff --check`.

---

### Task 4: Record, review, and integrate

**Files:**
- Create: `docs/verification/macos-screen-stability.md`
- Modify: `docs/development/current-stage.md`
- Modify: `docs/superpowers/plans/2026-08-12-macos-screen-stability.md`

**Interfaces:**
- Produces: one exact stage outcome and separates Mac evidence from the pending Windows rerun.

- [x] **Step 1: Write exact verification evidence**

Record platform, Git SHA, executable and artifact SHA-256 values, test counts,
durations, counters, geometry, codec implementation/status, the initial stale
binary diagnostic, the static-desktop false-positive diagnosis, and every
environment-dependent boundary.

- [x] **Step 2: Review the complete branch**

Inspect `origin/main...HEAD` for lifecycle leaks, callback/process cleanup,
Windows compatibility, redaction, artifact finalization, threshold changes,
generated files, secrets, and unrelated edits. Fix every Critical or Important
finding and rerun affected evidence.

- [x] **Step 3: Update the canonical handoff**

State that long-run macOS cadence evidence now requires owned dynamic content.
Keep the hardened Windows six-run rerun as the next Windows-only evidence task.

- [x] **Step 4: Commit documentation**

```bash
git add docs/verification/macos-screen-stability.md \
  docs/development/current-stage.md \
  docs/superpowers/plans/2026-08-12-macos-screen-stability.md
git diff --cached --check
git commit -m "docs: record macOS screen stability"
```

- [x] **Step 5: Verify, deliver, and clean**

Run fresh affected tests after the final commit. If all frozen gates pass,
push the feature branch, verify the remote SHA, fast-forward `main`, rerun the
merged Python/workflow gates, push `main`, verify `origin/main`, inspect ignored
state, and remove only `.worktrees/macos-screen-stability` plus its merged local
branch. Preserve the remote feature branch and all external WebRTC caches.
