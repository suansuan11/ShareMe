# Bounded Movie Video Pipeline and RSS Diagnostics

Status: approved for implementation after user review on 2026-08-05.

## Problem and evidence boundary

The active branch is `codex/movie-playback-performance` at `5901221`. The
branch already contains the receive-only viewer correction, direct FFmpeg
I420 conversion, a one-callback Qt preview adapter, and rejected hardware
experiments. The reported host RSS P95 near 3.80 GB came from a historical
quality-rejected H.264 experiment. It is not proof that the current source has
the same growth path.

Current source inspection establishes these boundaries:

- `PlaybackSession` has a video queue of capacity 3 and an audio queue of
  capacity 24.
- `VideoPreviewAdapter` admits at most one Qt queued callback.
- `FfmpegMediaSource` owns a `std::deque<MediaEvent>` between FFmpeg and
  `PlaybackSession`; its capacity protection is currently an uncommitted
  diagnostic change and is not yet a complete contract.
- FFmpeg decoder flush and audio resampler drain must remain correct when a
  bounded pending queue is temporarily full.
- Existing tests pass for the focused media and preview targets, but they do
  not expose the current/peak bytes or prove the flush boundary under a full
  pending queue.

The implementation must therefore measure the actual growth boundary before
claiming that a queue caused the RSS result.

## Goal

Make the movie media path explicitly bounded and observable without reducing
video viewing quality. Establish enough per-second evidence to distinguish
FFmpeg pending media, `PlaybackSession` queues, WebRTC codec counters, and Qt
preview delivery from process RSS.

## Immutable constraints

- Do not lower source or transmitted dimensions, cadence, bitrate, codec
  quality, chroma quality, or color fidelity.
- Do not add a new intentional frame-drop policy.
- Preserve existing `PlaybackSession` video `drop_oldest` behavior and report
  every occurrence; do not hide or reinterpret an existing drop.
- Preserve PTS, generation, seek, pause/resume, EOS, audio, voice, and WebRTC
  transport semantics.
- Keep movie audio, voice, and video lifecycles and queues independent.
- Keep `client/core` free of Qt, FFmpeg, WebRTC, GPU SDK, and OS dependencies.
- Do not enable or redesign VideoToolbox, H.264, P010, or Windows hardware
  paths in this stage.
- Keep raw movie files, logs, JSONL, traces, build output, local paths, and
  the external libwebrtc cache out of Git.
- Diagnostic output must not contain paths, room identifiers, SDP, ICE
  addresses, credentials, or device identifiers.

## Selected approach

Use source-level hard bounds with pull-based backpressure. FFmpeg stops
receiving decoded frames when the pending event capacity is reached. It does
not overwrite an event, reject an already-created event, or silently drop a
decoded frame. The next `read_next()` call resumes the same decoder after the
consumer has removed pending data.

This is preferred over a single-frame FFmpeg rewrite because it limits the
memory owner with a smaller change to the existing interleaving and decoder
state machine. A reference-counted cross-layer ring buffer is deferred because
it would expand the lifetime and Qt mapping risk before the RSS owner is known.

## Data flow and ownership

```text
FFmpeg AVFrame
  -> bounded pending media events
  -> PlaybackSession video/audio queues
  -> MovieVideoSource or MovieAudioSource
  -> WebRTC media path
  -> VideoPreviewAdapter, at most one queued callback
  -> QVideoSink
```

### FFmpeg pending events

`FfmpegMediaSource` keeps owned `VideoFrame` I420 planes and owned audio
samples. No `AVFrame`, `AVBufferRef`, or FFmpeg packet reference escapes the
source. `av_frame_unref` remains mandatory after every successful conversion
and all error paths.

The pending event storage has explicit per-kind capacities:

- video: 3 events;
- audio: 24 events;
- total: the sum of the two per-kind bounds.

The queue records its current event count, current owned capacity bytes, peak
event count, peak bytes, and backpressure count. Bytes are the capacities of
owned vectors, not a claim about allocator RSS.

### PlaybackSession queues

The existing video capacity of 3 and audio capacity of 24 remain unchanged.
The generic bounded queue gains optional item-size accounting supplied by the
media adapter, so the portable queue itself does not include media or third-
party headers. The video and audio queue snapshots expose capacity, current
size, current bytes, peak bytes, and existing dropped count.

### Qt preview

`VideoPreviewAdapter` retains the current one-in-flight atomic gate. A frame
submitted through the planar path keeps the WebRTC I420 buffer alive through
the Qt buffer object. The fallback remains one bounded ARGB copy. The adapter
exposes current pending callbacks and pending planar bytes; it never creates a
history list of Qt frames.

### WebRTC counters

When performance diagnostics are enabled, `SignaledPeer` reads video-only
WebRTC stats once per second on the signaling thread:

- host outbound `frames_encoded`;
- viewer inbound `frames_received`, `frames_decoded`, and `frames_dropped`.

If the stats request or field is unavailable, the counter line marks the
condition and omits the unavailable value rather than reporting a fabricated
zero. Stats polling is disabled during ordinary runs.

## Flush, drain, seek, and error semantics

Decoder draining returns whether it stopped because the pending queue was full
or because the decoder reached `EAGAIN`/`EOF`. A full queue is not completion.

Video and audio decoder state tracks independently:

- flush packet sent;
- decoder fully drained;
- pending events available;
- resampler fully drained for audio.

`EndOfStream` is returned only after all enabled decoders and the resampler are
fully drained and the pending event storage is empty. If a queue fills during
flush, the next `read_next()` drains pending events and resumes flushing.

`seek()` and `close()` clear pending events, update current byte counters, and
reset all flush/drain state. Cumulative decoded, dropped, backpressure, and
peak counters remain meaningful for the current source lifetime. Existing
generation filtering continues to reject stale events after a seek.

An allocation failure or invalid frame remains an existing decode failure. An
expected full queue is not an error and does not cause a frame drop.

## Diagnostic contract

The existing sanitized `PERF_COUNTERS version=1` line remains the transport
format. The allowlist is extended with these integer fields:

- `source_pending`, `source_pending_bytes`, `source_peak_pending`,
  `source_peak_pending_bytes`;
- `session_video_pending`, `session_video_bytes`, `session_audio_pending`,
  `session_audio_bytes`;
- `render_queue`, `pending_callbacks`, `pending_callback_bytes`;
- `owned_bytes`, `owned_peak_bytes`, `backpressure_events`,
  `stats_unavailable`.

Existing `decoded`, `offered`, `encoded`, `received`, `callback`, `submitted`,
`coalesced`, and `dropped` fields retain cumulative semantics. Role-specific
meaning is recorded in the implementation and verification document:

- host `decoded` is FFmpeg decoder output and `offered` is frames submitted to
  the local WebRTC video source;
- viewer `decoded` and `received` are inbound WebRTC video counters;
- `encoded` is the host outbound WebRTC video counter when available;
- `render_queue` and `pending_callbacks` are zero or one for the adapter;
- `owned_bytes` is the sum of the relevant application-owned pending buffers,
  not process RSS.

All records remain cumulative or snapshot values and are emitted once per
second only while `SHAREME_PERFORMANCE_COUNTERS` is set. The performance
runner adds the fields to its parser allowlist and tests their non-negative
and sanitized behavior. It does not change CPU, RSS, quality, or drop gates.

The existing uncommitted diagnostic prints that expose a movie path or emit
high-frequency unconstrained text are removed from the final implementation.

## Testing strategy

Tests are written first and must fail for the missing behavior.

### Portable queue and media tests

- Verify item-size accounting, current and peak bytes, FIFO order, capacity,
  and clear behavior in `BoundedQueue`.
- Verify `PlaybackSession` queue bounds and byte snapshots while preserving
  its existing drop count and generation behavior.
- Read FFmpeg fixtures through EOS and assert that bounded pending state never
  exceeds its per-kind limits.
- Add a flush-boundary regression that proves no tail video/audio events are
  lost when the pending queue becomes full.
- Verify seek clears pending bytes and rejects old generations.

### WebRTC and Qt tests

- Verify the preview adapter's callback and byte bounds before and after Qt
  event processing.
- Verify stats snapshots select video stats only and distinguish unavailable
  fields from zero counters.
- Keep the receive-only viewer directional regression and existing preview
  timestamp/lifetime tests.

### Script contract tests

- Accept every new sanitized integer field.
- Reject duplicate, negative, path-bearing, or unknown fields.
- Preserve existing aggregate and quality gates unchanged.

## Evidence and acceptance

The implementation stage is accepted only when:

- focused RED/GREEN tests and affected C++/Python suites pass;
- `git diff --check` passes and no raw artifacts or external cache changes are
  staged;
- a diagnostic 180-second software run, when the supplied `<MOVIE_PATH>` and
  native runtime are available, produces per-second host/viewer records;
- source and session queue depths stay at their declared bounds;
- current and peak owned buffer bytes are finite and attributable to named
  components;
- decoder, encoder, callback, submission, and render counters are either
  present with valid values or explicitly marked unavailable;
- no new source-level drops occur and existing drops remain separately visible;
- dimensions, cadence, metadata, PTS/generation behavior, audio, and preview
  regressions remain unchanged.

The 180-second run is environment-dependent without the supplied movie and a
native macOS runtime. It is diagnostic evidence, not proof of lower physical
temperature. A later quality-preserving performance gate remains blocked if
the current evidence does not include the required quality metrics.

## Scope and delivery

Implementation is limited to queue accounting, FFmpeg drain correctness,
media/playback snapshots, WebRTC diagnostic snapshots, Qt pending metrics, and
the sanitized performance parser/tests. Hardware acceleration, quality gate
thresholds, drift correction, hard resync, signaling redesign, audio format,
and user-visible playback controls are out of scope.

The design document is committed separately. Production changes are delivered
in focused commits from the existing ignored feature worktree, with the
pre-existing uncommitted diagnostic changes reviewed and either incorporated
or replaced without modifying unrelated work.
