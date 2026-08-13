# macOS Physical Sleep Continuity Design

## Problem

The physical clamshell run proved that macOS emitted one generation of
`screen-locked -> will-sleep -> did-wake -> screen-unlocked`. The host then
restarted ScreenCaptureKit once and resumed encoding on the next counter sample.
Despite that successful recovery, the base screen runner rejected the artifact
because it counted the intentionally suspended samples between the lifecycle
start and the post-resume recovery marker as an ordinary in-call video stall.

The artifact is correctly failed under the current contract. It must not be
relabelled or accepted retroactively.

## Decision

Teach the base continuity validator about explicit, role-aligned excluded sample
ranges supplied by a scenario observer. The lifecycle observer derives one
inclusive range per role from the first required generation-1 suspension event
through that role's single generation-1 recovery marker. The validator omits
only those samples and resets its comparison state across the boundary.

This is preferred over the rejected alternatives:

- Increasing the global five-sample stall limit would weaken every screen smoke
  run and hide real regressions.
- Running lifecycle validation after bypassing base validation would duplicate
  quality checks and permit incomplete artifacts.
- Using wall-clock sleep duration would depend on sampling phase and would not
  prove that the application observed or recovered from the same generation.

## Contracts

- No exclusion is inferred from elapsed time, system power logs, process pause,
  or a lone notification.
- Each role must expose the complete required event sequence in generation 1
  and exactly one same-generation recovery marker before an exclusion exists.
- The excluded range begins at the counter position of the first required event
  and ends at the counter position of the recovery marker, inclusive.
- Counter readiness, availability, monotonicity, and the existing maximum of
  five consecutive stalled included samples remain unchanged outside the range.
- The first included sample after an exclusion starts a new comparison segment;
  cross-boundary equality or regression is not evaluated because capture was
  intentionally suspended and may have been recreated.
- Lifecycle validation still independently requires host and viewer video plus
  all primary-voice counters to advance within five samples after recovery and
  requires at least ten post-resume samples.
- Multiple generations, missing events, missing/duplicate markers, malformed
  ranges, and any unbounded post-resume stall fail closed.
- Production controller, ScreenCaptureKit, WebRTC, VideoToolbox, signaling,
  voice, dimensions, cadence, bitrate, queues, and presentation recovery are not
  changed.

## Interfaces

`run_screen_stream_smoke._validate_continuous_progress` and
`validate_records` accept optional role-specific inclusive sample ranges. The
returned continuity evidence adds `excluded_samples` while retaining
`warmup_samples`, `observed_samples`, and `max_stall_samples`.

`LifecycleScenario.continuity_exclusions(host_reader, viewer_reader)` returns
`{"host": [(start, end)], "viewer": [(start, end)]}` only after validating the
same generation and unique recovery marker. `run_smoke` requests these ranges
before base validation and passes them through without interpreting lifecycle
semantics itself.

## Verification

TDD must first reproduce the physical artifact shape: more than five unchanged
video samples inside a causal excluded range, immediate post-marker recovery,
and uninterrupted voice counters. The test must fail against the current
validator and pass only after exclusions are implemented. Separate tests must
prove that stalls before or after the range still fail and that missing or
generation-mismatched lifecycle evidence cannot create an exclusion.

After unit and repository gates pass, rerun a fresh attended clamshell sleep
call. Only a new complete artifact with real macOS Sleep/Wake log evidence,
H.264 VideoToolbox, matching geometry, exactly one bounded host capture restart,
video and bidirectional voice recovery within five samples, and at least ten
post-resume samples may be accepted.

## Evidence boundary

The final claim is limited to the tested Mac, clamshell sleep/wake, and the
runner's observed native notifications. It does not verify lid-open menu sleep,
permission revocation, display removal, audible voice, physical scanout,
thermals, Windows, or automatic signaling reconnect/rejoin.
