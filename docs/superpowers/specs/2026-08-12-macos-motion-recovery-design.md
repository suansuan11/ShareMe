# macOS Screen Motion Recovery Design

## Outcome

Add a deterministic macOS screen-sharing recovery gate that proves an active
call survives a bounded interruption of changing screen content. The host and
viewer must keep bidirectional synthetic voice alive, retain the current
VideoToolbox H.264 and quality gates, and resume video progress within five
seconds after the controlled content producer resumes.

## Selected approach

The existing smoke runner owns `shareme_screen_motion_fixture`. For an optional
recovery probe, the runner sends `SIGSTOP` to that process after a configured
number of measured seconds and sends `SIGCONT` after a short configured
interval. The fixture window remains visible but stops producing motion; the
product capture, codec, transport, decode, and presentation processes remain
running throughout.

The runner records only sanitized phase names and elapsed seconds. It never
records the fixture path, process ID, room, child output, or user information.
Cleanup always resumes a stopped fixture before terminating its process group.

This approach is preferred over UI scripting because it needs no Accessibility
or Automation permission, and over a ScreenCaptureKit restart hook because the
current evidence does not show a native stream failure. It is also preferred
over loosening continuity thresholds: all current media and quality gates stay
unchanged.

## Interfaces

`scripts/run_screen_stream_smoke.py` gains:

- `MotionInterruption(after_seconds: int, duration_seconds: int)` as a narrow
  validated configuration value;
- `validate_motion_interruption(...)` to reject unsupported platforms, missing
  fixtures, intervals outside the measured run, durations over five seconds,
  or insufficient post-recovery observation;
- `suspend_motion_fixture(process)` and `resume_motion_fixture(process)`, using
  `SIGSTOP` and `SIGCONT` only on macOS;
- `validate_motion_recovery(host_records, viewer_records, resume_sample,
  deadline_samples=5)` to require every role's video counters to advance by the
  recovery deadline and to require voice counters to advance across the probe;
- CLI options `--motion-interruption-after-seconds` and
  `--motion-interruption-duration-seconds`, which must be supplied together and
  require `--motion-fixture`;
- sanitized JSONL run/phase/summary fields describing the requested boundary,
  actual suspend/resume elapsed seconds, and measured recovery samples.

The default smoke path and Windows acceptance behavior remain unchanged when
the two new options are absent.

## Data flow and lifecycle

1. Validate the interruption configuration before opening the artifact.
2. Start and verify the owned fixture, signaling server, host, and viewer using
   the existing lifecycle.
3. Start the measured scenario clock only after both peers exist.
4. At the configured boundary, verify the fixture is alive and send `SIGSTOP`.
5. At the configured resume boundary, send `SIGCONT` and continue the call.
6. Keep sampling peer resources and checking every guard process. A deliberately
   stopped fixture is alive and therefore remains a valid guard.
7. At call end, terminate peers and parse their once-per-second counters.
8. Apply all existing screen gates, then apply the extra pre/probe/post recovery
   gate. The final post-recovery observation window must be at least ten seconds.
9. In every failure or interruption path, send `SIGCONT` if needed before normal
   process-group cleanup.

## Recovery gate

For both host and viewer:

- counters must already be ready before the interruption;
- host `encoded`, `callback`, and `submitted`, and viewer `received`, `decoded`,
  `callback`, and `submitted`, must each exceed their resume-boundary values no
  later than five counter samples after resume;
- all four bidirectional voice packet/byte counters must exceed their
  pre-interruption values after the probe;
- at least ten counter samples must remain after resume;
- the existing no-regression and maximum-five-stall-sample gates remain active
  over the full call.

A missing boundary, early fixture exit, failed signal, peer exit, missing
counter, counter regression, late recovery, or absent post-recovery window is a
hard failure. No-data evidence is never interpreted as recovery.

## Test and verification strategy

- RED/GREEN unit tests cover configuration validation, exact signals,
  resume-before-cleanup, sanitized phase evidence, recovery success at the
  five-sample boundary, and failures for late/missing video or voice progress.
- Existing screen smoke, Windows acceptance, GUI smoke, and process metric
  suites remain green under system and Homebrew Python where available.
- Fresh `call-dev` CTest and repeated `signaled_peer` lifecycle remain green.
- A native macOS standard-profile call runs at least 60 seconds, suspends the
  owned fixture for three seconds after 15 seconds, and passes every existing
  and new gate with VideoToolbox active.
- Go race/vet, workflow tests, skill validation, portable core scan, artifact
  redaction review, and `git diff --check` pass.

## Boundaries

This verifies recovery from a deterministic pause in dynamic screen content
while the native call remains active. It does not prove actual app
minimize/restore, occlusion semantics, display sleep/wake, screen lock,
ScreenCaptureKit `didStopWithError` restart, physical scanout, subjective image
quality, audible voice, thermal behavior, Windows, or 4K. Those remain
environment-dependent or unimplemented and must be reported separately.

No capture dimensions, cadence, codec, bitrate, queue bound, drop policy, or
quality threshold is reduced. Generated artifacts and build output remain
ignored, and the external libwebrtc cache remains read-only.
