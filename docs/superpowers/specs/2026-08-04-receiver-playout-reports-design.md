# Receiver Playout Reports Design

## Goal

Measure the movie frame submitted to the viewer's Qt video sink, map it to the host's
absolute movie timeline, and report it to the host without applying correction.
Seek generations must prevent old frames and reports from contaminating the new
timeline.

## Media-to-control anchor

`MovieVideoSource` records one atomic sample for every emitted frame:
absolute media PTS, 32-bit 90 kHz RTP timestamp, and timeline generation. The
host publishes the latest same-generation RTP timestamp with `playback-state`.
It waits for a same-generation sample after a seek instead of pairing the new
movie PTS with an old transport timestamp.

The viewer retains the accepted playback-state anchor and the local monotonic
time at which it arrived. When Qt accepts a submitted remote frame, the viewer converts
the signed modulo-32-bit RTP delta into milliseconds at 90 kHz and adds it to
the anchor media PTS. Deltas outside ten seconds are rejected as unanchored.

## Playout report

While playing, the viewer publishes at most one `playout-report` every 250 ms.
The report contains rendered absolute PTS, nonnegative estimated buffer delay,
viewer-local monotonic receive time, and generation. Encoding validates room,
sequence, JSON-safe integer ranges, and a buffer range of 0 through 10000 ms.

The host accepts only increasing report sequences whose generation equals its
current movie generation. It compares current host timeline PTS with reported
rendered PTS and exposes the observed delta and the existing `SyncController`
decision in the sender UI. No decision changes playback in this stage.

## Lifecycle and error handling

- A new playback generation invalidates the viewer's prior rendered sample.
- Reports are suppressed until both a same-generation host anchor and a frame
  submitted to the Qt sink are available. This is not proof of display scanout.
- Paused state may publish one final report but does not project the host PTS.
- Malformed, wrong-room, stale-sequence, stale-generation, and out-of-window
  messages/samples are ignored without closing the call.
- No audio, queue, playback-rate, buffer, or hard-resync policy changes.

## Verification

Pure tests cover message validation, RTP wrap in both directions, generation
reset, stale report rejection, and sync-decision observation. Movie-source tests
cover same-generation frame samples after seek. Native RTC tests, full CTest,
Go signaling tests, workflow validation, and a supplied-movie macOS smoke close
the stage. Windows behavior remains environment-dependent.
