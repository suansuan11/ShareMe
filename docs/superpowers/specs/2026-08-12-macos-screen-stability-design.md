# macOS Screen Stability Evidence Design

## Outcome

Build a truthful macOS long-run screen-sharing gate that distinguishes an
unchanged desktop from an actual ScreenCaptureKit, VideoToolbox, or WebRTC
stall. The product media path stays unchanged; the test runner owns a
deterministic moving-screen precondition and fails if that precondition stops.

## Evidence behind the design

The same current macOS binary produced two different outcomes:

- without controlled screen motion, a 30-second standard run advanced at first
  and later failed because host video counters stopped changing while voice
  counters continued;
- with `shareme_screen_motion_fixture` visible and moving for the full run, the
  30-second standard run completed with active VideoToolbox H.264, 1638 host
  encoded frames, 1631 viewer decoded frames, and zero stalled observation
  samples.

This isolates the failure to the smoke precondition rather than demonstrating
a product encoder failure. ScreenCaptureKit is content-driven; a continuity
gate cannot infer an encoder stall when the test does not guarantee changing
screen content.

## Selected approach

Extend the generic screen-smoke command with an optional motion-fixture path.
When supplied, the runner starts the fixture before either RTC peer, confirms
that it remains alive, includes it in the existing guard-process checks, and
terminates it deterministically after the call. The JSONL summary records only
sanitized fixture lifecycle state, not absolute paths, process IDs, window
titles, usernames, room IDs, or child output.

The fixture window must also raise itself and request activation when its QML
root is completed, while retaining `WindowStaysOnTopHint`. A live fixture
process is insufficient evidence if its moving window is occluded and therefore
does not produce changing screen content.

Long-run macOS stability evidence must use the fixture option. A run without a
fixture remains supported for short functional diagnosis, but it cannot be
described as proving sustained encode cadence on an unchanged desktop.

## Rejected approaches

### Permit long video-counter stalls

Rejected because increasing or disabling the five-sample continuity limit
would hide real capture, encoding, transport, decode, or presentation stalls.

### Synthesize repeated product frames

Rejected because repeated frames alter product behavior, consume additional
CPU/GPU/encoder power, and conflict with the quality-preserving thermal goal.

### Change ScreenCaptureKit configuration

Rejected because the controlled experiment passed with the existing native
capture configuration. There is no evidence that queue depth, pixel format,
frame interval, or capture lifecycle is defective.

## Interfaces

`scripts/run_screen_stream_smoke.py` gains:

- CLI option `--motion-fixture PATH`;
- `start_motion_fixture(path, profile, duration_seconds, environment)` that
  starts `PATH --profile PROFILE --duration-seconds DURATION` in its own
  process group with output suppressed;
- `run_smoke(..., motion_fixture: Path | None = None)` or an equivalent narrow
  orchestration seam that starts the fixture before the host, adds it to
  `guard_processes`, and owns teardown;
- a run field stating that the fixture was requested and summary fields stating
  whether it started, stayed alive for the measurement, and stopped during
  cleanup.

Existing Windows acceptance may continue launching its own fixture and passing
it through `guard_processes`; no Windows behavior or artifact schema consumer
may be broken. Supplying both an externally guarded fixture and the new owned
fixture is unnecessary and should be rejected or kept impossible through the
public interface.

## Lifecycle and errors

1. Validate the executable path before opening the output artifact.
2. Start the motion fixture with a duration long enough to cover runner startup,
   the requested call duration, and bounded teardown.
3. After a short condition-based readiness interval, fail with
   `motion-fixture-early-exit` if the process has exited.
4. Start signaling, host, and viewer using the existing flow.
5. Reuse `require_guard_processes_alive` during room creation and every sample
   interval so a mid-run fixture exit immediately fails the run.
6. Terminate the owned fixture in `finally`, including all partial-start and
   interruption paths.
7. Record sanitized lifecycle booleans in the final JSONL summary. Do not copy
   raw fixture diagnostics into evidence.

## Acceptance gates

- RED/GREEN unit tests prove command construction, early exit, mid-run guard
  failure, deterministic cleanup, and redacted artifact output.
- Existing screen-smoke and Windows acceptance suites remain green under both
  the macOS system Python and Homebrew Python.
- Fresh `call-dev` builds and full CTest pass against the preserved external
  WebRTC archive.
- A fixture-owned native standard run passes for at least 30 seconds with H.264,
  VideoToolbox active, matching geometry, nonzero bitrate, zero continuity
  stalls, bidirectional synthetic voice, and one viewer presentation recovery.
- A fixture-owned native stability run passes for at least 120 seconds under
  the same gates.
- Go race/vet, workflow 8/8, skill validation, portable-core header scan, and
  `git diff --check` pass.

## Boundaries

This stage verifies macOS test orchestration and the local native media path. It
does not verify Windows Media Foundation, physical scanout, subjective visual
quality, audible voice, physical temperature, foreground/background recovery,
display sleep, cursor composition, display selection, or 4K. It does not lower
any resolution, cadence, bitrate, queue, or quality threshold.

Generated builds, JSONL, fixture logs, and process output remain ignored.
External WebRTC checkout, build archives, depot tools, and caches remain
read-only.
