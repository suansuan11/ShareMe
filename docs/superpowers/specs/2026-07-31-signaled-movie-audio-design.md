# Signaled Movie Audio and Sender A/V Timing Design

## Goal

Add a host-only `movie-audio` WebRTC track carrying real decoded movie sound
while retaining the existing independent host and viewer voice tracks. Prove
48 kHz stereo PCM reception and sender-side movie A/V timing on macOS ARM64
without routing movie sound through the microphone device or voice processing
chain.

## Scope

This slice:

- opens the host movie independently for video-only and audio-only decoding;
- resamples movie audio to signed 16-bit, 48 kHz stereo PCM;
- rechunks decoded PCM into exact 10 ms WebRTC audio frames;
- sends the PCM through a dedicated unprocessed `movie-audio` track;
- retains the existing processed microphone `host-voice` and `viewer-voice`
  tracks;
- preserves source-track offsets by pacing both movie sources from one shared
  monotonic epoch and the same container start PTS;
- verifies decoded remote movie PCM without enabling speaker playout in the
  two-process automation; and
- keeps the call-only build independent of FFmpeg.

This slice does not add speaker device routing, volume UI, pause, resume, seek,
subtitle synchronization, viewer playout reports, hard resync, TURN,
public-network testing, or Windows native verification.

## Alternatives

### Selected: independent stream decoders with a shared timeline

`MovieVideoSource` and `MovieAudioSource` each open the same file, decode only
their required stream, and share a `MovieTimeline` epoch. Both FFmpeg instances
report the same container start PTS, so original audio/video track offsets are
preserved. This retains the stable Stage 5 video boundary and keeps the audio
implementation testable in isolation.

The cost is opening and demuxing one file twice. That cost is acceptable for
this verification stage because unused packets are discarded without opening
the unused decoder. A future measured optimization may replace both adapters
with one demux coordinator.

### Rejected: one shared FFmpeg coordinator

A single coordinator would avoid duplicate demuxing and provide one central
queue, but it would require replacing the newly verified movie-video lifecycle
and materially increase the regression surface before audio transport is
proven.

### Rejected: inject movie PCM through the audio device module

This would reuse the microphone capture path, but it would mix movie sound with
voice and risk applying AEC, noise suppression, or automatic gain control. It
violates the three-track isolation requirement.

## Components

### FFmpeg stream selection and media origin

`FfmpegMediaSourceOptions` gains independent `decode_video` and `decode_audio`
flags. At least one flag must be true. Opening a requested but unavailable
stream produces a typed `VideoStreamUnavailable` or `AudioStreamUnavailable`
failure. `MediaInfo` exposes `start_time_ms`, derived from the container start
time and normalized to zero only when FFmpeg reports no start time.

Audio-only mode opens only the best audio decoder, ignores video packets, and
supports files with or without video. Video-only mode retains the Stage 5
behavior and ignores audio packets. The default opens both streams and remains
compatible with the Qt playback path.

### Shared movie timeline

`MovieTimeline` owns one optional `steady_clock` epoch. Its `start()` operation
sets the epoch exactly once and returns the same value to both movie sources.
The host creates one timeline and captures it in both source factories.

Each source schedules media PTS `p` at:

```text
shared_epoch + (p - container_start_time_ms)
```

Late frames are emitted immediately. Stop requests interrupt pending waits.
This retains a source file's original audio/video offset even when the audio
stream begins before the video stream or both begin at a nonzero timestamp.

### PCM rechunking

`PcmChunker` accepts only 48 kHz, stereo, signed 16-bit interleaved
`AudioFrame` values. It produces exact 480-sample-per-channel chunks. It carries
partial decoded frames forward in a bounded buffer and assigns each output
chunk a PTS based on the first input sample plus the number of emitted samples.

Discontinuities larger than one 10 ms frame reset pending samples and begin a
new chunk timeline. Invalid sample rate, channel count, or non-interleaved
sample count is rejected with a stable local error. No digital gain, limiter,
or time-stretching is added in this slice.

### Movie audio WebRTC source

`LocalAudioSource` defines lifecycle, counters, last emitted PTS, and sanitized
error access in addition to `webrtc::AudioSourceInterface`.
`MovieAudioSource` implements it using an audio-only `PlaybackSession`, the
shared timeline, and `PcmChunker`.

The source publishes 16-bit, 48 kHz, two-channel, 480-frame callbacks to the
sink registered by WebRTC. Its `AudioOptions` explicitly disable echo
cancellation, noise suppression, and automatic gain control. The source never
touches the audio device module used by the microphone track.

### Signaled peer integration

`SignaledPeerConfig` gains an optional injected movie-audio source factory.
Only a host may configure it. The peer creates a second audio track named
`movie-audio` and adds it alongside `host-voice`. The viewer still sends only
`viewer-voice`.

Startup establishes the shared epoch, starts movie video, then movie audio. A
failure in either movie source fails the call without substituting synthetic
media. Shutdown stops and joins both movie sources before releasing their
tracks, PeerConnection, and WebRTC runtime.

Inbound tracks are distinguished by stable track identifiers. The automation
attaches a counting PCM sink to `movie-audio`; voice tracks remain separate.
Speaker playout stays disabled to prevent acoustic feedback while host and
viewer run on the same Mac.

## CLI and smoke contract

The signaled call probe adds `--movie-audio`. It is valid only for a host using
`--video movie --movie <path>`. The call-only binary returns
`movie-audio-dependency-unavailable` without accepting or exposing the path.
The smoke orchestrator passes the path and flag only to the host.

Sanitized result output adds:

- received movie-audio frames;
- received movie-audio sample rate and channel count;
- a non-silence peak level;
- host generated movie-audio chunks; and
- host sender movie A/V PTS skew.

It never prints file paths, SDP, candidates, addresses, credentials, tokens, or
device identifiers.

## Error model

Stable source categories are:

- `movie-audio-open-failed`;
- `movie-audio-unavailable`;
- `movie-audio-decode-failed`;
- `movie-audio-frame-invalid`;
- `movie-audio-source-unavailable`; and
- `movie-audio-dependency-unavailable`.

Errors never contain a local path or FFmpeg detail and never trigger a fallback
to microphone or synthetic audio.

## Verification

Automated tests use generated files and real FFmpeg decoding:

1. audio-only FFmpeg selection decodes stereo PCM and rejects a video-only file
   with `AudioStreamUnavailable`;
2. default and video-only FFmpeg modes retain existing playback/video behavior;
3. `PcmChunker` emits exact 480-by-2 chunks, preserves sample order and PTS,
   carries a partial frame, and resets on a discontinuity;
4. `MovieAudioSource` emits paced, non-silent 48 kHz stereo 10 ms frames,
   reports monotonic PTS, maps missing/no-audio/decode failures, and stops
   promptly during a future PTS wait;
5. signaled-peer tests prove invalid role/factory handling, distinct track
   creation, and no change to the call-only dependency boundary; and
6. a two-process host/viewer smoke run carries real movie video, real movie
   audio, and bidirectional microphone voice through the Go signaling service.

Acceptance requires:

- the viewer receives at least 100 movie-audio PCM callbacks;
- every callback is 48 kHz, stereo, 480 frames, and at least one is non-silent;
- the viewer receives at least 20 movie-video frames at 320x180;
- voice RTP remains nonzero in both directions;
- the host's latest emitted movie audio/video PTS differ by at most 50 ms;
- call-only, playback-only, combined, Go race, and Go vet suites pass; and
- no generated fixture, build artifact, cache, local path, SDP, or log is
  committed.

This proves sender-side A/V pacing and remote decoded movie PCM transport. It
does not prove audible speaker output, independent volume controls, remote
render-time A/V error, or cross-machine/public-network behavior.
