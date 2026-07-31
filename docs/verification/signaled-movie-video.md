# Signaled Movie Video Verification

## Verified scope

On 2026-07-31, a host macOS ARM64 process opened a generated local movie with
FFmpeg, decoded RGBA frames through `PlaybackSession`, converted them to I420
with libyuv, and sent them as the existing `movie-video` WebRTC track. An
independent viewer process joined through `QtSignalingClient` and the local Go
service without receiving or opening the movie path.

The same run retained native microphone capture on both peers. Movie frames
used media PTS for 90 kHz RTP ordering and a monotonic clock for delivery.
Missing, video-less, decode, conversion, and dependency failures use sanitized
categories and never fall back to a test pattern.

## Build and repeatable commands

```bash
cmake --fresh --preset movie-call-dev \
  -DWEBRTC_ROOT=/Users/dio/Library/Caches/ShareMe/webrtc \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev --output-on-failure

python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server \
  --port 18105 \
  --audio synthetic \
  --video synthetic

python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server \
  --port 18106 \
  --audio microphone \
  --video movie \
  --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
```

CTest creates untracked fixtures with the installed FFmpeg executable: the main
320x180, 30 fps, two-second MPEG-4/AAC movie, an audio-only file, a movie
starting at PTS 5 seconds, and a movie with a 5-second internal PTS gap. The
current slice intentionally ignores movie audio. The timing fixtures verify
first-PTS normalization and prompt, interruptible shutdown.

The smoke script gives the path only to the host. It requires the viewer to
report at least 20 received movie frames at exactly 320x180, plus nonzero
bidirectional voice RTP and positive local microphone levels. Output contains
only a temporary room code, dimensions, counters, candidate type, and stable
errors; it excludes paths, tokens, SDP, candidates, addresses, and credentials.

## Recorded results

Synthetic regression:

```text
ROOM PO7B3K
RESULT connected=1 video=59 width=640 height=360 audio_sent=103 audio_received=103 audio_level=0.244148 candidate=host error=
RESULT connected=1 video=59 width=640 height=360 audio_sent=103 audio_received=103 audio_level=0.244148 candidate=host error=
```

Host movie plus bidirectional microphones; the viewer result is printed first:

```text
ROOM UT3LNN
RESULT connected=1 video=56 width=320 height=180 audio_sent=103 audio_received=101 audio_level=0.0168462 candidate=host error=
RESULT connected=1 video=61 width=640 height=360 audio_sent=101 audio_received=101 audio_level=0.0168462 candidate=host error=
```

Combined CTest passed 22/22. The separate call-only build passed 11/11 and
therefore still builds without FFmpeg. Invalid host/viewer/movie option
combinations return exit code 2. A call-only binary asked for movie mode returns
`movie-video-dependency-unavailable` and does not silently substitute synthetic
media.

Environment:

- macOS 26.6 (25G72), Apple silicon ARM64
- CMake 4.3.3, Apple Clang, and Ninja
- Qt 6.11.1
- FFmpeg libraries 8.1.1 as resolved through Homebrew pkg-config
- Go 1.26.5
- locked libwebrtc revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`

## Not verified

- movie audio transport or audible remote playout
- host local rendering, pause, seek, subtitles, or application UI integration
- hardware H.264 encoding, GPU zero-copy, adaptation, reconnect, or endurance
- two physical computers, TURN, or public-network connectivity
- Windows native FFmpeg/libwebrtc build and device behavior

This proves the direct decoded-movie video path on the recorded Mac
environment. It does not prove complete movie playback, production voice
quality, public-network operation, or Windows media support.
