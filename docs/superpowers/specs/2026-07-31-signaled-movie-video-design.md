# Signaled Movie Video Design

## Goal

Prove the product's primary media path on macOS: only the host opens a local
movie, FFmpeg decodes its video frames, libwebrtc transports those frames
through the existing Qt/Go signaled call, and the viewer verifies receipt. Keep
the existing synthetic video mode as a deterministic regression path and keep
bidirectional microphone voice unchanged.

This slice intentionally excludes movie audio. A distinct stereo PCM WebRTC
track must be designed separately so movie samples never enter the microphone
ADM or AEC/NS/AGC path.

## Selected approach

Introduce a small `LocalVideoSource` contract inside the libwebrtc adapter.
`TestPatternSource` and a new `MovieVideoSource` both implement it. A
`SignaledVideoMode` plus factory callback selects the source without making the
base WebRTC target depend unconditionally on FFmpeg.

`MovieVideoSource` lives in a separate optional target that links both
`ShareMe::Playback` and `ShareMe::WebRTC`. It owns a `PlaybackSession`, advances
the playhead from a monotonic clock, converts decoded FFmpeg RGBA frames to I420
with libyuv, and emits WebRTC frames paced by their media PTS. The bounded
queues and generation checks remain owned by `PlaybackSession`.

Alternatives rejected:

- adding FFmpeg directly to `shareme_webrtc` would make every WebRTC build
  require FFmpeg and weaken the existing optional-dependency boundary;
- adding movie audio in the same slice would either conflate it with voice or
  require a second audio injection architecture before video is independently
  proven;
- screen capture would bypass the direct-file pipeline required by the product.

## Components and data flow

```text
host --movie sample.mp4
  -> FfmpegMediaSource
  -> PlaybackSession bounded video queue
  -> MovieVideoSource RGBA-to-I420 conversion and PTS pacing
  -> host movie-video WebRTC track
  -> Qt/Go SDP and ICE signaling
  -> viewer CountingVideoSink and inbound RTP stats

host microphone <-> existing processed voice tracks <-> viewer microphone
```

The viewer does not open the movie. For this transitional headless probe it
keeps the existing test-pattern outbound track so both peer directions retain
the established lifecycle and stats coverage. Product UI semantics can remove
the viewer video sender when the application facade replaces the probe.

## Configuration and build boundary

Add a `movie-call-dev` configure/build/test preset that enables Qt, FFmpeg, and
WebRTC together. Existing `dev`, `playback-dev`, `webrtc-dev`, and `call-dev`
presets remain unchanged.

The signaled call CLI gains:

```text
--video synthetic|movie
--movie /absolute/or/relative/path
```

`--video movie` is valid only for the host and requires a readable movie path.
The viewer remains `synthetic`; the smoke orchestrator passes the movie only to
the host. Invalid combinations exit with code 2. Media open/decode/conversion
failures use sanitized categories and never fall back to a test pattern.

## Timing and lifecycle

The first decoded video PTS establishes the media origin. Frames are emitted
when monotonic elapsed time reaches `frame.pts_ms - first_pts_ms`; late frames
are delivered immediately and the existing bounded queue drops stale frames.
WebRTC capture timestamps use the monotonic delivery time, while RTP timestamps
derive from media PTS at 90 kHz so ordering remains deterministic.

Shutdown order is:

1. cancel and join the signaled result waiter;
2. stop `MovieVideoSource` pacing;
3. close and join `PlaybackSession`;
4. release WebRTC tracks and PeerConnection proxies;
5. stop WebRTC runtime threads.

All stop operations are idempotent. Source errors surface through the existing
peer failure callback.

## Verification

Unit and integration tests must prove:

- RGBA frames convert to I420 with the correct dimensions and nonempty planes;
- movie source rejects missing or video-less input with typed failure;
- decoded frame PTS values are monotonic and at least 20 frames are emitted
  from a generated two-second 320x180, 30 fps fixture;
- signaled configuration rejects movie mode without a source factory;
- existing synthetic and microphone signaled calls still pass;
- a host using the generated movie and a viewer using no local file connect
  through the Go service, report host ICE, nonzero bidirectional voice RTP, and
  at least 20 viewer-received movie frames;
- Go race/vet, default CTest, playback CTest, call CTest, and the new combined
  movie-call CTest all pass.

The smoke output remains sanitized: it may report counters, dimensions, media
PTS, candidate type, and stable errors, but never tokens, SDP, candidates,
addresses, credentials, or local movie paths.

## Explicit exclusions

- movie audio transport or audible remote playout
- local host rendering/control UI, pause, seek, and subtitles
- hardware H.264 encoding or zero-copy GPU frames
- TURN/public-network connectivity, reconnect, adaptation, or endurance
- Windows native FFmpeg/libwebrtc verification
