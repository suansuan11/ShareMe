# Movie Playback Three-Stage Boundary Design

Date: 2026-08-05

Status: approved for specification review

## Goal

Improve the movie playback path in three independently verifiable stages:

1. establish a safe software video-path default and truthful capability
   reporting;
2. add an application-owned movie-audio clock and a bounded, observable video
   scheduler; and
3. add cross-platform movie-audio output-route monitoring without rebuilding the
   movie peer, video peer, or voice path.

The stages preserve the existing transport, signaling, queue, voice, independent
media, and quality contracts. They do not deliver hardware video encoding. A
separate hardware proposal may later request a new locked WebRTC dependency, but
that work is outside this design and must use a separate branch.

## Evidence And Current Boundary

Current source and Git state outrank older handoffs. The verified baseline is
`main` at `b463676` (`fix: make movie diagnostics nonblocking`). The repository
external libwebrtc cache is preserved and read-only for this work.

Current source establishes these constraints:

- `WebRtcRuntime` registers the libvpx VP8 encoder and decoder template adapters
  in `client/rtc/webrtc/src/webrtc_runtime.cpp`.
- The locked dependency uses `rtc_use_h264=false`; this stage must not change the
  manifest, revision, GN arguments, ABI, or cache.
- `FfmpegMediaSource` can select the existing macOS VideoToolbox decoder in an
  explicitly selected `auto` mode, but its output is transferred and converted
  to owned I420 planes.
- `SyncController` currently returns a decision, while
  `RtcDemoController::receiveControlMessage` only records
  `host_sync_action_`; no correction is applied.
- `MovieAudioPeer` currently enables native ADM playout for the viewer. The new
  renderer replaces that movie-audio playout while voice remains on the primary
  WebRTC ADM.
- The locked WebRTC `RemoteAudioSource` passes
  `std::nullopt` for `absolute_capture_timestamp_ms`, so a viewer audio callback
  does not carry the sender's media PTS.
- `VideoPreviewAdapter` submission to `QVideoSink` is application-layer
  telemetry. It is not display scanout, compositor presentation, or acoustic
  audio/video proof.

## Immutable Constraints

- Preserve source and transmitted resolution, cadence, bitrate policy, codec
  quality, color metadata, queue bounds, and existing quality gates.
- Preserve independent movie audio, host voice, and viewer voice lifecycles,
  queues, devices, and verification claims.
- Keep `MovieAudioPeer` responsible for independent WebRTC movie-audio
  transport. It must not own a persistent second PCM queue.
- Keep voice capture and playout on the existing WebRTC ADM path. Voice never
  flows through `MovieAudioRenderer`.
- Keep `client/core` free of Qt, FFmpeg, libwebrtc, GPU SDK, and operating-system
  headers.
- Keep audio clock, PTS mapping, route generation, video scheduling, correction
  policy, and failure classification in Qt-free value contracts and algorithms.
- Keep `QAudioSink`, `QMediaDevices`, CoreAudio, Windows endpoint APIs, and
  PipeWire/PulseAudio adapters outside the portable core.
- Keep WebRTC callbacks nonblocking. No callback waits for Qt, an output device,
  signaling, or a renderer lock with unbounded duration.
- Preserve the external libwebrtc checkout, manifest, revision, ABI, and cache.
- Do not add fake hardware adapters, enable `rtc_use_h264`, replace the current
  VP8 factory, or claim hardware encoding in these stages.
- Do not unify `MovieVideoSource` and `MovieAudioPeer` into a monolithic movie
  pipeline.
- Do not put the media clock or scheduling algorithm in
  `RtcDemoController`. It may orchestrate lifecycle, protocol anchors, status,
  and diagnostics only.

## Selected Approach

Use incremental extensions of existing boundaries.

The rejected alternatives are:

- A unified movie pipeline would replace independently verified media paths and
  create a broad regression surface.
- Controller-only correction cannot provide an actual audio-consumption clock,
  route-safe handoff, or a media-owned scheduling algorithm.

The selected approach adds stable value contracts in `client/core`, keeps
transport in `client/rtc/webrtc`, keeps movie sources in `client/rtc/movie`, and
puts Qt/native output and route adapters in application/tool layers.

## Architecture And Ownership

### Stage 1 ownership

`FfmpegMediaSource` owns the requested decoder mode and reports the selected
decoder path through `MediaInfo` and the existing movie format/metrics path.
`WebRtcRuntime` owns a fixed codec capability report for the current locked
factory. `RtcDemoController` joins these reports for sanitized diagnostics.

Stage 1 may add stable capability and reporting value types plus minimal mode
selection seams. It must not add unused abstract encoder factories, native-frame
interfaces, or fake platform implementations. Real hardware encoder and
native-frame integration interfaces belong in the later hardware proposal.

### Stage 2 ownership

`MovieAudioPeer` owns the dedicated WebRTC movie-audio transport and exposes a
nonblocking PCM callback. The callback receives a bounded, synchronous view of
the decoded block and a receiver sequence; it copies directly into the
renderer-owned queue. The peer owns no persistent PCM queue for the viewer.

`MovieAudioRenderer` owns:

- the bounded ready PCM ring;
- accepted-but-not-consumed in-flight blocks;
- media-PTS mapping;
- `logical_consumed_frames`;
- clock validity and confidence;
- `renderer_queue_duration` and `device_queue_duration`;
- playback generation, host audio epoch, and viewer-local renderer clock epoch;
- route-transition state; and
- renderer counters and stable failure events.

`AudioOutputDevice` reports backend facts and performs device operations. It
does not own media PTS, logical consumption, route generation, or correction
policy.

`client/core` owns pure clock, mapper, scheduler, policy, token-lifecycle, and
failure-classification contracts. A Qt/WebRTC adapter owns actual audio and
video payloads and implements output/device operations.

### Stage 3 ownership

`AudioRouteMonitor` observes route changes. `AudioRouteController` creates and
activates candidate output devices. `MovieAudioRenderer` performs the atomic
handoff and remains the only owner of media continuity. Neither route component
rebuilds a peer or touches voice ADM state.

`RtcDemoController` wires callbacks, protocol anchors, renderer and scheduler
lifecycle, user-visible status, and diagnostics. It never calculates media
clock positions or decides frame disposition.

## Stage 1: Safe Video-Path Boundary

### Requested mode and active path

The movie video acceleration default becomes `software` in all defaults:

- `FfmpegMediaSourceOptions`;
- `MovieVideoSource` convenience constructors;
- `RtcDemoController` state and selection;
- the RTC demo CLI option; and
- script/test contracts that omit the option.

`auto` remains valid only when the caller explicitly selects it. The existing
experimental decoder behavior may report a hardware decoder path when it is
actually selected, with software fallback when it is not. This does not imply a
hardware WebRTC encoder.

The sanitized report has independent fields:

```text
requested_mode=software|auto
decoder_path=software|hardware|fallback
webrtc_encoder=vp8-software
hardware_encoder_status=unavailable-locked-abi
```

The report must not overload one `path` field to represent both decoder and
encoder behavior. An explicitly selected `auto` decoder path must never produce
`webrtc_encoder=hardware`.

### Stage 1 acceptance

Tests must prove:

- omitted mode selects software;
- explicit `software` selects software;
- explicit `auto` remains experimental and reports the actual decoder path;
- fallback is distinct from software requested mode;
- the WebRTC encoder remains `vp8-software`; and
- hardware encoder status is unavailable because of the locked ABI.

The stage preserves all dimensions, cadence, bitrate, color, metadata, queue,
drop, and quality checks. Focused media/WebRTC/CLI tests, the affected Python
contract tests, full CTest, and `git diff --check` are required. Results are
reported separately for macOS and Windows; source inspection never verifies
native Windows media behavior.

## Stage 2: App-Owned Audio Clock And Scheduling

Stage 2 has two independent production gates.

### Stage 2A: renderer and candidate policy

Stage 2A may be committed after focused automated verification. It contains the
app-owned renderer, bounded PCM ownership, output-device facts, renderer clock
snapshots and confidence, provisional-correlation blocking, an observational
video scheduler, pure policy tests, candidate telemetry, token lifecycle, and
deterministic stall tests.

Production hold, late-drop, and hard-resync wiring remains disabled. When the
audio correlation is not locked, the production scheduler remains
observational/pass-through and reports `clock_blocked` rather than correcting.

### Stage 2B: correction execution

Stage 2B is not committed or enabled until both the frozen drift gate and the
quality gate pass. Before that checkpoint, production emits candidate telemetry
only. The frozen drift gate is the existing gate in
`docs/superpowers/specs/2026-08-04-drift-study-bounded-hard-resync-design.md`:

- at least 900 accepted reports per complete run outside pause;
- no sequence or generation regression;
- no report gap outside pause over two seconds;
- steady/post-recovery absolute P99 delta below 300 ms;
- at least 95 percent of steady/post-recovery samples within 100 ms;
- every resume and seek recovery reaches three consecutive samples within 100
  ms within five seconds; and
- no call, decode, RTC, or native audio failure.

The quality gate retains the existing exact geometry, cadence, metadata,
PSNR/SSIM, drop/coalescing, audio, pause/seek, CPU/RSS, and bounded-backlog
requirements. Missing evidence fails the gate.

### Output-device contract

The Qt-free output contract has these operations:

```text
open(format) -> open result
start() -> start result
try_write(pcm) -> WriteResult
snapshot() -> AudioDeviceSnapshot
pause()
stop()
```

`WriteResult` distinguishes:

- `accepted` with a positive per-call frame count;
- `would_block` with zero accepted frames; and
- `failed` with zero accepted frames and a stable failure category.

The device snapshot contains only backend facts:

```text
accepted_frames_total
device_consumed_frames_total
device_queue_frames
optional output_latency_frames
underrun_count
discontinuity_count
last_discontinuity_reason
active
```

Per-call `accepted_frames` is never confused with cumulative
`accepted_frames_total`. `device_consumed_frames_total` is monotonic only within
one output-device instance and route generation. It describes backend
consumption, not guaranteed acoustic presentation. Device totals reset for a
new device instance; the renderer's `accepted_frames_total` is cumulative for
the renderer lifetime, while `logical_consumed_frames` remains the continuous
cross-route consumption counter.

### Renderer clock contract

`MovieAudioRenderer` exposes:

```text
try_enqueue(pcm_view, receiver_sequence) -> EnqueueResult
set_playback_anchor(anchor)
pump(monotonic_now)
activate_output(device) -> ActivationResult
deactivate_output(reason)
snapshot() -> MovieAudioRendererSnapshot
```

Its snapshot includes:

```text
accepted_frames_total
device_consumed_frames_total
logical_consumed_frames
renderer_queue_duration
device_queue_duration
estimated_playout_pts_ms
clock_confidence
playback_generation
host_audio_epoch
renderer_clock_epoch
route_generation
underrun_count
discontinuity_count
last_discontinuity_reason
```

Clock confidence is one of:

```text
unavailable | provisional | locked | degraded | invalid
```

`logical_consumed_frames` is renderer-owned and continuous across successful
route changes. It advances only from verified device-consumption deltas. The
renderer may subtract output latency only when the latency is trusted and
valid; otherwise the clock remains provisional or degraded. It never treats
bytes written as consumed frames.

The ready ring contains frames not accepted by the device. In-flight blocks
contain accepted but not yet consumed frames. These populations are reported
separately, so `renderer_queue_duration` and `device_queue_duration` are not
double-counted.

The callback copies once directly into the bounded ready ring. A full ring is a
nonblocking `audio-queue-overflow` event, increments a discontinuity counter,
and invalidates confidence. It never silently claims continuous playback.

### PTS mapping and correlation

Every audio anchor contains:

```text
control_sequence
playback_generation
audio_epoch
host_source_sequence
media_pts_ms
sample_rate
channel_count
```

The control sequence is strictly increasing. A stale playback generation,
audio-epoch regression, format change, control sequence regression, or excessive
anchor residual invalidates the mapper.

The reliable DataChannel is not ordered relative to RTP audio. An anchor alone
cannot identify the decoded PCM block to which it belongs. The mapper therefore
has an explicit correlation state and may become `locked` only after receiving
either:

- a source sequence and decoded-block sequence available on both sides; or
- a separately verified bounded-error estimator explicitly accepted at the
  Stage 2B checkpoint.

The current locked WebRTC API does not provide the sender timestamp through
`RemoteAudioSource`, so no unverified timestamp inference is permitted.
Provisional or invalid mapping supports diagnostics only and cannot drive video
correction.

`playback_generation` identifies host media seek/source state. `host_audio_epoch`
identifies the host audio stream epoch. `renderer_clock_epoch` is viewer-local
and changes when local consumption is unknown or the renderer must re-anchor. A
route change does not change either host value.

### Portable video scheduler

The scheduler operates on opaque tokens:

```text
VideoFrameTiming { token, media_pts_ms, playback_generation }
VideoClockInput { clock_confidence, audio_playout_pts_ms,
                  playback_generation, route_generation, playing }
```

`VideoFrameTiming.media_pts_ms` comes from the existing same-generation,
wrap-safe RTP/video-anchor mapper. Missing, stale, or invalid video anchors do
not permit correction. `QVideoSink` submission remains a separate
application-level telemetry value and is never used as the audio clock or as
proof of display presentation.

Frame disposition, clock state, and scheduler events are separate types. A
disposition applies to a frame token; a scheduler event may apply to an episode
or lifecycle transition:

```text
FrameDisposition: pass_through | hold | present | drop
SchedulerEvent: clock_blocked | candidate_started | candidate_cancelled |
                hard_resync_applied | hard_resync_exited | route_transition
```

Every removal returns the token and a reason to the adapter for exactly-once
payload release. This covers late drops, queue overflow, generation reset,
hard-resync flush, route reset, and shutdown.

The scheduler uses:

```text
delta_ms = video_media_pts_ms - audio_playout_pts_ms
```

Positive values mean video is early. Negative values mean video is late. The
non-overlapping threshold and hysteresis constants are frozen as follows:

- early-hold enters at `delta_ms >= +50` and exits at `delta_ms <= +25`;
- late-drop enters at `delta_ms <= -50` and exits at `delta_ms >= -25`;
- hard-resync candidacy requires periodic `delta_ms <= -300`; and
- candidacy cancellation uses `delta_ms > -250`.

With a locked clock, early frames are held, frames in the presentation band are
presented, and late frames are dropped with an explicit late-drop counter. A
severely positive delta never qualifies for viewer-local late-video hard
resync. Early holding is bounded to three tokens and 250 ms, whichever is
reached first. At the bound, the scheduler removes the affected token with
`early-hold-limit` telemetry rather than retaining an unbounded queue.

Hard-resync candidacy consumes ordered periodic observations, not every video
submission. It resets on playback-generation change, route-generation change,
clock-confidence loss, discontinuity, pause, or sequence invalidation.

Hard resync is viewer-local. When enabled, it flushes queued tokens older than
the target audio PTS and waits for the next same-generation frame at or after
that PTS. It has a two-second maximum wait and a 120-frame discard limit.
Exit precedence is frozen:

```text
generation_changed
route_transition
clock_lost
end_of_stream
frame_limit
timeout
applied
```

`hard_resync_applied` is emitted only after successful same-generation
reacquisition. Before Stage 2B passes its gates, a qualified episode emits
`candidate_started` only; it cannot be reported as an applied correction.

The controller and diagnostics expose two separate viewer action values:
`viewer_suggested_action` records the current candidate or policy suggestion,
while `viewer_applied_action` records only a completed hold, late drop, or
successful hard resync. There is no single `hostSyncAction` value that can make
an observation appear to be an executed correction. A hard-resync candidate is
never copied into `viewer_applied_action`.

### Route-transition video behavior

Route transition is distinct from ordinary `clock_blocked` pass-through.

- The renderer invalidates correction immediately when the old output is
  quiesced.
- The scheduler holds at most three frames or 250 ms, whichever comes first.
- The stale pre-switch audio clock is never used.
- At the limit, released frames may use pass-through only with
  `clock_blocked` and `route_transition` telemetry.
- A new route must be active before a new `route_generation` is committed.
- Correction remains disabled until the new renderer clock is at least locked.

## Stage 3: Cross-Platform Audio Routing

### Route interfaces

The common route layer exposes:

```text
AudioRouteMonitor.start(callback)
AudioRouteMonitor.stop()
AudioRouteController.on_route_notification()
AudioRouteController.activate_candidate()
```

Qt `QMediaDevices` notification is the common device-list/default-output
layer. Native adapters supplement it where needed:

- Windows `IMMNotificationClient::OnDefaultDeviceChanged`;
- macOS CoreAudio default-output property notification; and
- Linux PipeWire/PulseAudio route notification.

Native headers remain in platform adapter targets. The portable route policy
receives only value events and candidate activation results.

### Atomic handoff

The route transaction is:

1. Receive a route notification. Do not increment `route_generation`.
2. Coalesce duplicate notifications and create a candidate output device.
3. Quiesce writes to the old device and take one final atomic snapshot.
4. Retire fully consumed blocks.
5. Trim a partially consumed block and retain only its unconsumed suffix.
6. If exact consumption is unavailable, record
   `route-handoff-unknown-consumption`, increment `renderer_clock_epoch`, and
   invalidate confidence.
7. Open, start, and confirm the candidate active.
8. Requeue the retained suffix and atomically commit the candidate. Only at
   this point increment `route_generation`.
9. Re-anchor the renderer clock using the same media PTS and host generation.
10. Stop and retire the old device.

If activation fails, resume the old route when possible. If neither route is
active, leave the renderer inactive and keep all peers alive. A candidate that
disappears during activation follows the same failure path and never commits a
generation.

### Route and lifecycle verification

Automated tests cover:

- notifications without generation changes;
- successful activation committing exactly one generation;
- failed activation and old-route resume;
- partial-block trimming and unknown-consumption invalidation;
- renderer/device queue-duration separation;
- underrun and stable discontinuity reasons;
- PCM callbacks during output teardown;
- route notifications during shutdown;
- repeated route changes;
- immediate candidate-device loss;
- bounded callback completion;
- exactly-once token release; and
- unchanged movie-audio peer identity, voice ADM identity, and voice transport
  state across every route transaction.

Manual/native verification is reported separately for portable contracts, Qt
notifications, native-adapter builds, native-device switching, and physical
acoustic behavior. One platform never verifies another.

## Failure Taxonomy

One authoritative enum-to-string and enum-to-impact mapping is used by core
snapshots, renderer status, diagnostics, and tests. The impact classes are:

```text
transient | recoverable | degraded | fatal-to-renderer | fatal-to-call
```

The initial stable categories are:

| Category | Impact |
| --- | --- |
| `audio-output-would-block` | transient |
| `audio-output-failure` | recoverable |
| `audio-queue-overflow` | degraded |
| `audio-consumption-unknown` | degraded |
| `audio-correlation-unavailable` | degraded |
| `audio-correlation-residual-exceeded` | degraded |
| `audio-format-change` | recoverable |
| `route-activation-failed` | recoverable |
| `route-handoff-unknown-consumption` | degraded |
| `route-no-active-output` | fatal-to-renderer |
| `hard-resync-timeout` | degraded |
| `hard-resync-clock-lost` | degraded |
| `hard-resync-route-transition` | degraded |
| `hard-resync-frame-limit` | degraded |
| `hard-resync-end-of-stream` | recoverable |
| `movie-audio-transport-failure` | fatal-to-call |

`audio-output-would-block` is a normal transient state and is never promoted
to a call failure by itself. Fatal-to-call remains reserved for the existing
transport/peer failure categories, not local device changes.

## Threading And Lifecycle

- WebRTC audio callbacks call the renderer's bounded nonblocking ingress and
  return within a bounded time.
- The renderer's output operations and `QAudioSink` calls run on their owning
  Qt/output thread.
- Route monitor callbacks are marshalled to the route controller's owning
  thread before device lifecycle operations.
- Core policy consumes immutable snapshots and returns value decisions; it does
  not call Qt, WebRTC, FFmpeg, or native APIs.
- Shutdown first disables callback ingress, then quiesces the renderer and
  route monitor, releases every queued token exactly once, and only then stops
  the peers. Late callbacks observe a closed ingress and do not wait.
- Concurrent teardown, route changes, PCM callbacks, and peer shutdown are
  tested with bounded completion and repeated lifecycle runs.

## Delivery And Git Boundaries

Each stage uses its own ignored feature worktree and has one isolated merge and
rollback boundary. A stage may contain multiple focused commits, but no commit
may mix Stage 1, Stage 2, and Stage 3 responsibilities.

Delivery order is:

1. Stage 1 software boundary and capability reporting.
2. Stage 2A renderer, clock, candidate scheduler, and automated verification.
3. The frozen drift and quality checkpoint.
4. Stage 2B production correction execution, only if the checkpoint passes.
5. Stage 3 route monitoring and output replacement.

At each boundary, run focused tests, affected suites, full relevant CTest,
`git diff --check`, and platform acceptance proportional to the claim. Update
`docs/development/current-stage.md` only at the completed boundary. Do not stage
build output, generated artifacts, raw media, logs, dependency caches, secrets,
or local settings.

## Hardware-Encoding Follow-Up

Hardware encoding is explicitly unimplemented after these stages. The product
must report `webrtc_encoder=vp8-software` and
`hardware_encoder_status=unavailable-locked-abi`.

A later proposal must use a separate branch and may request a new locked WebRTC
manifest/revision and dependency rebuild. It must include:

- reproducible dependency bootstrap;
- macOS VideoToolbox encoder/decoder verification;
- Windows Media Foundation/D3D11 encoder/decoder verification;
- codec negotiation and software fallback;
- native-frame transport;
- resolution, cadence, bitrate, color, PSNR/SSIM, CPU, RSS, thermal, and
  audio/video synchronization gates; and
- separate platform evidence with no cross-platform inference.

Until that proposal passes its platform gates, no current product report may
describe hardware WebRTC video encoding as available or delivered.
