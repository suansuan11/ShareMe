# Player / Receiver Control Slice Design

## Goal

Deliver the first production-shaped receiver slice through the existing Qt RTC
demo: a host can select a movie, the viewer renders its WebRTC video, and a
reliable ordered `shareme-control-v1` data channel carries validated,
host-authoritative playback-state snapshots to the viewer UI.

This stage proves media plus control state over the same peer connection. It
does not claim viewer-authoritative pause/seek, clock synchronization, remote
speaker playout, TURN, or public-network acceptance.

## Selected approach

Extend the already working `shareme_rtc_demo` rather than combining the local
FFmpeg player and RTC lifecycles in `shareme_playback_demo`. The RTC demo already
owns signaling, peer lifetime, and receiver video presentation; the headless
signaled-call tool already proves the reusable movie sources. This produces a
small vertical slice without introducing a second decoder into the local player.

Alternatives rejected for this stage:

- Directly embed WebRTC into `shareme_playback_demo`: correct long-term product
  direction, but it couples local audio/video clocks, RTC source lifetimes, and
  view switching before the control transport is proven.
- Implement bidirectional pause/seek and full synchronization now: the movie
  sources currently have no pause/seek API, so this would require a broader
  concurrency and authority redesign.

## Architecture

### Control transport

`SignaledPeer` gains an opaque text control-message callback and a host-only
send operation. The host creates one reliable, ordered data channel named
`shareme-control-v1`; the viewer accepts only that label. Binary, empty, and
messages larger than 64 KiB are rejected. Callbacks are disabled before peer
teardown and the observer is unregistered on the WebRTC signaling thread.

The WebRTC layer transports text but does not parse application JSON. This
keeps Qt JSON and room-specific protocol state out of the portable peer layer.

### Playback-state codec

A small QtCore-backed codec builds and validates the documented version-1
`playback-state` envelope. It checks the room identifier, positive sequence,
allowed state, signed millisecond fields, rate range, and nonnegative
generation. A viewer-side tracker rejects duplicate/out-of-order sequences and
older generations before exposing state.

### Movie host and receiver UI

The RTC demo CLI accepts `--source movie --movie PATH` and optional
`--movie-audio`, only for the host. It reuses `MovieVideoSource`,
`MovieAudioSource`, and a shared `MovieTimeline` when audio is enabled.

Once the control channel is open, the movie host publishes a `playing` snapshot
immediately and once per second. Position is derived from the shared monotonic
movie timeline. The viewer displays the last accepted remote state and media
position as read-only information beside the rendered video. The periodic
snapshot is reconciliation evidence, not a synchronization guarantee.

## Error handling and lifecycle

- CLI rejects invalid role/source/movie combinations without printing the
  movie path.
- Failure categories remain sanitized.
- Data-channel callbacks use an independently shared active state; they never
  retain the controller or peer after `stop()` disables callbacks.
- Qt controller callbacks are queued to its thread, and its timer stops before
  peer teardown.
- Invalid/stale playback messages do not alter viewer state.

## Verification

- Pure codec/tracker unit tests cover valid round trips, malformed envelopes,
  room mismatch, stale sequence, and stale generation.
- `SignaledPeer` tests cover config policy and public send validation; existing
  peer tests protect shutdown and negotiation behavior.
- RTC demo CLI tests cover accepted movie syntax, rejected combinations, and
  path redaction.
- Build and complete CTest run on macOS, plus local Go signaling race/vet.
- A local two-process GUI smoke run is attempted with a generated movie and is
  reported separately from automated verification.

Windows reruns remain required for the affected native build and GUI path.
