# Drift Study and Bounded Hard Resync Design

## Goal

Produce reproducible evidence for ShareMe viewer timeline drift, then implement
the first bounded hard-resync command only if the evidence passes a predefined
safety gate. The stage must not change `SyncController` thresholds, media queue
policies, movie-audio/voice separation, or claim display scanout measurement.

## Stage structure

This is one long stage with a mandatory evidence checkpoint:

1. add typed drift samples, deterministic aggregation, opt-in JSONL capture,
   and a scripted playback measurement scenario;
2. run the supplied movie through three five-minute macOS clean-local sessions
   and publish a compact evidence report;
3. evaluate the measurement gate;
4. only if the gate passes, implement the bounded hard-resync protocol and
   policy, then rerun the affected verification;
5. if the gate fails, stop without implementing automatic correction and
   publish the failed gate plus the next required experiment.

The checkpoint is not optional. Passing unit tests cannot replace measurement.

## Measurement meaning

The existing viewer position is recorded after a remote frame is submitted to
the Qt video sink. It is evidence of application-level sink submission, not
proof of compositor presentation, display scanout, or audible A/V alignment.

Each accepted host-side sample records:

- schema version and monotonic sample index;
- local monotonic capture time in milliseconds;
- playback generation and playout-report sequence;
- host authoritative movie PTS and viewer submitted movie PTS;
- signed viewer delta (`hostPtsMs - viewerPtsMs`);
- viewer-reported buffer milliseconds;
- current playback state and observed `SyncAction`;
- selected ICE candidate type when available; and
- scenario phase (`warmup`, `steady`, `paused`, `post-resume`,
  `post-forward-seek`, `post-backward-seek`, or `cooldown`).

No token, SDP, ICE credential, absolute media path, room secret, or raw media is
written. The room identifier is omitted from committed evidence. JSONL capture
is explicit through a host-only CLI option and defaults off.

## Scripted measurement profile

The first deterministic profile is `drift-study-v1` and requires a movie with
at least 330 seconds remaining from its start PTS. This covers 300 seconds of
wall-clock execution plus the net forward-seek displacement:

| Elapsed time | Action or phase |
| ---: | --- |
| 0-30 s | warm-up; exclude from steady percentiles |
| 30-90 s | steady playback |
| 90 s | pause for 5 s |
| 95-150 s | resume and recovery |
| 150 s | seek forward by 60 s, bounded to movie duration |
| 150-210 s | forward-seek recovery and steady playback |
| 210 s | seek backward by 30 s, bounded to movie start |
| 210-300 s | backward-seek recovery and steady playback |

The controller owns the scripted action timer on its Qt thread. Movie sources
continue to obey `MovieTimeline`; the scenario must not call video and audio
sources separately. A run fails if room connection is lost, media ends early,
an action is rejected, generation changes by an unexpected amount, the report
stream regresses, or the output cannot be finalized atomically.

## Aggregation and evidence

Aggregation is deterministic portable C++20. For each complete run and for the
combined sample set it reports:

- accepted/rejected sample counts and report coverage;
- signed delta min/max/mean and absolute P50/P95/P99/max;
- counts for every `SyncAction`;
- generation transitions and stale-generation rejection counts;
- pause/resume and seek recovery duration, defined as time until three
  consecutive reports have `abs(delta) <= 100 ms`;
- missing-report gaps, with the largest gap; and
- hard-resync candidate episode count using the policy below, without applying
  a command during the measurement phase.

The ignored raw artifacts live below an explicit build root such as
`build/drift-study/`. The repository commits only the procedure, summary tables,
artifact SHA-256 values, tool/build identifiers, platform scope, and exact
commands. Do not commit JSONL, movies, logs, caches, or machine-local paths.

## Measurement gate

All conditions must pass across three complete five-minute macOS clean-local
runs before automatic hard resync may be implemented:

- every run has at least 900 accepted reports outside the paused interval;
- no sequence or generation regression is accepted;
- no report gap outside pause exceeds 2 seconds;
- steady/post-recovery absolute P99 delta is below 300 ms;
- at least 95 percent of steady/post-recovery samples have
  `abs(delta) <= 100 ms`;
- every resume, forward seek, and backward seek returns to three consecutive
  `abs(delta) <= 100 ms` reports within 5 seconds; and
- no call, decode, RTC, or native audio failure is captured.

Failure of any condition blocks automatic correction in this stage. The Agent
must record the failed metric and stop; it must not loosen the gate, change
queue/timing policy, or reinterpret missing evidence as success.

macOS evidence does not verify Windows. A Windows run remains
environment-dependent unless executed on that machine with the same profile.

## Candidate bounded hard-resync policy

The candidate policy is frozen before measurement so it cannot be tuned to make
one dataset look successful:

- consider only ordered reports for the current generation while host state is
  `playing` and both video anchor fields are present;
- require `SyncController::decide(delta).action == hard_resync` for four
  consecutive reports spanning at least 750 ms;
- cancel the episode on pause, generation change, missing anchor, stale report,
  connection change, or any intervening non-hard-resync decision;
- target the current bounded host timeline PTS, not the stale viewer PTS;
- increment `MovieTimeline` generation exactly once through one accepted seek;
- publish one reliable ordered `sync-command` containing target PTS and new
  generation;
- viewer invalidates prior generation measurement state immediately and waits
  for a same-generation playback anchor before reporting again;
- enforce a 10-second cooldown after each command;
- allow at most three automatic commands per call; reaching the limit disables
  automatic correction and exposes a stable failure/status value; and
- define recovery as three consecutive current-generation reports within
  `abs(delta) <= 100 ms` inside 5 seconds.

The feature is opt-in in the RTC demo for this stage. Default calls continue to
observe and display decisions without applying correction.

## Sync-command protocol

The version-1 ordered control envelope uses:

```json
{
  "version": 1,
  "type": "sync-command",
  "roomId": "ABC234",
  "sequence": 73,
  "payload": {
    "action": "hard-resync",
    "targetPtsMs": 125600,
    "generation": 5
  }
}
```

Validation requires a valid room, positive JSON-safe sequence, action exactly
`hard-resync`, JSON-safe target PTS, and JSON-safe generation. The viewer
accepts only increasing command sequences and generation exactly one greater
than the last accepted playback generation. The command alone never makes the
viewer reportable; a matching playback-state frame anchor is still required.

## Testing and acceptance boundaries

TDD covers aggregation, percentile rounding, phase boundaries, report gaps,
candidate episodes, cooldown, attempt exhaustion, pause/generation reset,
codec validation, overflow, and old/future command rejection. DataChannel tests
cover the command round trip. Movie timeline tests prove one generation change
per accepted resync seek.

The completed stage may claim:

- verified portable aggregation and bounded policy tests;
- verified macOS application-level drift evidence for the named profile;
- verified control-channel command delivery if exercised; and
- partial GUI/media correction unless visible telemetry and recovery are
  observed in a real run.

It must not claim Windows verification, display scanout timing, acoustic A/V
sync, TURN behavior, public-network behavior, or changed performance targets
without their own evidence.
