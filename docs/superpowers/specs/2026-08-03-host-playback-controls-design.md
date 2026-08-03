# Host Playback Controls Design

## Goal

Add host-authoritative pause, resume, and seek controls to the movie path used
by `shareme_rtc_demo`. Movie video and independent movie audio remain separate
decoders, but both obey one thread-safe timeline and publish the resulting
state, absolute media PTS, and seek generation to the viewer.

This stage proves local host control and receiver state reconciliation. It does
not claim viewer-authoritative controls, receiver speaker playout, playout
reports, frame-generation filtering, hard resync, TURN, public-network
acceptance, or Windows native verification.

## Selected approach

`MovieTimeline` becomes the only owner of the host movie clock. Both movie
sources keep their independent `PlaybackSession`, queue, and lifecycle, but
sample the shared timeline before decoding and use the same generation when a
seek occurs.

This is preferred over having `RtcDemoController` call video and audio sources
separately: two independent calls could partially succeed and temporarily put
the tracks on different playheads. A combined FFmpeg demux coordinator is also
rejected for this stage because it would replace already verified media paths
and broaden the regression surface.

## Shared timeline contract

`MovieTimeline` owns the following state under one mutex:

- initialized container start PTS and duration;
- `playing` or `paused` state;
- an absolute media-PTS anchor and matching `steady_clock` time;
- seek `generation`, initially zero and incremented exactly once per accepted
  seek; and
- a revision counter used only to wake waiters after pause, resume, or seek.

The first source initializes the timeline from `MediaInfo`. A later source must
provide the same start PTS and duration or fail with a sanitized timeline
mismatch. The valid range is `[startPtsMs, startPtsMs + durationMs]`, computed
with checked arithmetic; seek targets outside it are rejected without mutation.

A snapshot computes the current absolute media PTS from the anchor and elapsed
steady time while playing; paused snapshots keep the frozen anchor. Pause and
resume change the revision but not generation. Seek changes the absolute
anchor, increments both generation and revision, preserves the prior
playing/paused state, and wakes all sources.

Timeline waits are condition-based and stop-token-aware. A source waiting for a
future PTS wakes on pause/resume/seek and recalculates its deadline. It returns
to decoding only when the target PTS is due, the generation changed, or stop was
requested. No polling queue or unbounded command backlog is introduced.

## Movie-source behavior

On start, each movie source opens its stream, initializes or validates the
shared timeline, and records generation zero. During its worker loop it:

1. samples the timeline;
2. pauses its `PlaybackSession` and blocks when the timeline is paused;
3. on a new generation, seeks its own session to the shared absolute PTS;
4. recreates the audio `PcmChunker` after an audio seek so pre-seek samples
   cannot leak into the new generation;
5. advances the session playhead from the timeline snapshot; and
6. emits each frame/chunk only after the shared timeline says its PTS is due.

Frames or audio chunks decoded for an older generation are discarded before
delivery. Existing bounded media queues and overflow policies remain unchanged.
Movie pause never pauses host or viewer voice tracks.

If the video source reaches EOF, the controller publishes one final `paused`
state at the last emitted PTS, as in the current receiver-control stage. Seeking
after EOF is outside this stage because the current source worker has ended; UI
commands are disabled once the movie source reports `kEnded`.

## Controller, protocol, and UI

For a movie host, `RtcDemoController` exposes read-only host state, position,
duration, generation, and control availability, plus invokable pause, resume,
and absolute seek operations. Invalid role, missing movie, unavailable peer,
ended source, and out-of-range targets are rejected without changing state.

The host view adds Pause/Resume buttons and a bounded position slider. The
slider presents normalized elapsed milliseconds but converts back to absolute
media PTS before calling the controller. Viewer controls remain absent.

`playback-state` continues to use the documented absolute `mediaPtsMs`. Its
`generation` comes from `MovieTimeline`; pause/resume keep it unchanged and seek
increments it. State is published immediately after an accepted command and on
the existing periodic reconciliation timer. The viewer's current tracker
rejects lower generations and non-increasing channel sequences.

The documented `sync-command` is deliberately not emitted in this stage. The
viewer currently has neither playout reports nor a generation-aware receive
buffer, so clearing or seeking its `QVideoSink` would not prove a hard resync.

## Error handling and lifetime

- Timeline initialization overflow or source mismatch fails with a sanitized
  media-source category; paths and decoder details are not exposed.
- Control methods execute on the Qt thread; source workers access only the
  synchronized timeline API.
- Stop requests interrupt timeline waits, after which existing peer shutdown
  joins both sources before releasing WebRTC state.
- A failed seek in either `PlaybackSession` remains a source failure and fails
  the call; it does not silently resume one track.

## Verification

TDD acceptance on macOS includes:

- deterministic timeline unit tests for initialization, pause freeze, resume,
  bounded seek rejection, generation/revision changes, overflow rejection, and
  stop-token-aware waits;
- movie-video integration proving frame delivery freezes while paused, resumes,
  and jumps to the requested nonzero absolute PTS after seek;
- movie-audio integration proving callback delivery freezes, resumes, discards
  pre-seek chunks, and follows the same generation/PTS target;
- playback-state tests proving pause/resume preserve generation and seek
  publishes the incremented generation;
- RTC demo target/QML build and CLI contract;
- complete movie-call CTest, Go race/vet, workflow 8/8, skill validation, and
  `git diff --check`.

Timing assertions only establish control correctness with generous bounds;
they are not performance claims. Windows build and GUI acceptance remain
environment-dependent until rerun on Windows hardware.
