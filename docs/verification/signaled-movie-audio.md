# Signaled Movie Audio Verification

## Verified scope

On 2026-07-31, a macOS ARM64 host opened one generated movie through
independent FFmpeg video-only and audio-only decoders. The decoders shared one
monotonic `MovieTimeline`: video was converted to I420 for the `movie-video`
track, while audio was normalized to 48 kHz stereo S16, rechunked to exact
10 ms blocks, and sent through an independent unprocessed `movie-audio` Opus
track. Host and viewer microphone voice tracks remained separate with native
AEC, noise suppression, and automatic gain control enabled.

The viewer never received the movie path. It attached a counting sink only to
the remote `movie-audio` track and verified decoded PCM callbacks without
enabling speaker playout. The host reported generated chunks and the absolute
difference between the latest emitted movie-audio and movie-video PTS values.
The CLI and smoke output contain only dimensions, numeric counters, candidate
type, and stable error categories; they exclude paths, SDP, ICE candidates,
addresses, tokens, and credentials.

## Fixture and repeatable commands

CTest generates a two-second MP4 using FFmpeg lavfi sources:

- MPEG-4 video, 320x180, 30 fps, YUV 4:2:0;
- AAC input audio, 48 kHz mono, decoded and normalized by ShareMe to 48 kHz
  stereo S16;
- container start time 0 seconds and duration 2 seconds.

The movie suite also creates audio-only, video-only, nonzero-start, negative
timeline, 44.1 kHz resampling, five-second PTS-gap, and staggered A/V fixtures.
Those fixtures cover stream selection, exact rechunking, resampler drain,
checked timestamp arithmetic, preserved stream offsets, and interruptible
shutdown.

```bash
cd server
go test -count=1 -race ./...
go vet ./...
cd ..

cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev --output-on-failure
cmake --build --preset build-call-dev
ctest --preset test-call-dev --output-on-failure
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev --output-on-failure

python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server \
  --port 18123 \
  --audio synthetic \
  --video synthetic

python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server \
  --port 18124 \
  --audio microphone \
  --video movie \
  --movie-audio \
  --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
```

`--movie-audio` is valid only for a host using `--video movie --movie <path>`.
Viewer, missing-path, and non-movie combinations return exit code 2 without
printing the path. A call-only binary given the otherwise valid combination
prints exactly `PEER_ERROR movie-audio-dependency-unavailable` and exits 1.
Movie video without `--movie-audio` remains supported.

## Recorded results

All verification commands above exited zero. CTest passed 6/6 portable-core
tests, 19/19 playback tests, 11/11 call-only tests, and 33/33 combined
movie-call tests. Go race tests passed all five tested internal packages plus
the command package with no tests, and `go vet ./...` exited zero.

Synthetic two-process regression; viewer result is first:

```text
ROOM UGC6XF
RESULT connected=1 video=60 width=640 height=360 audio_sent=103 audio_received=102 audio_level=0.244148 movie_audio_frames_received=0 sample_rate=0 channels=0 peak=0 chunks_generated=0 movie_av_skew_ms=0 candidate=host error=
RESULT connected=1 video=59 width=640 height=360 audio_sent=102 audio_received=101 audio_level=0.244148 movie_audio_frames_received=0 sample_rate=0 channels=0 peak=0 chunks_generated=0 movie_av_skew_ms=0 candidate=host error=
```

Movie video, independent movie audio, and bidirectional microphones; viewer
result is first:

```text
ROOM 3Y6YXN
RESULT connected=1 video=58 width=320 height=180 audio_sent=149 audio_received=299 audio_level=0.0182806 movie_audio_frames_received=302 sample_rate=48000 channels=2 peak=3448 chunks_generated=0 movie_av_skew_ms=0 candidate=host error=
RESULT connected=1 video=59 width=640 height=360 audio_sent=299 audio_received=102 audio_level=0.0908841 movie_audio_frames_received=0 sample_rate=0 channels=0 peak=0 chunks_generated=200 movie_av_skew_ms=23 candidate=host error=
```

This exceeds the acceptance thresholds of 100 viewer movie-audio callbacks,
48 kHz stereo with nonzero peak, 100 generated host chunks, at most 50 ms
sender-side A/V PTS skew, 20 received 320x180 movie frames, and nonzero
bidirectional voice RTP.

## Environment and review fixes

- macOS 26.6 (25G72), Apple silicon ARM64
- CMake 4.3.3, Ninja 1.13.2, Apple Clang 21.0.0
- Qt 6.11.1 and FFmpeg 8.1.1
- Go 1.26.5 and Python 3.9.6
- locked libwebrtc revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`

The existing repository-external cached libwebrtc dependency was preserved and
not cleaned. Independent review fixes added real edge fixtures, bounded PCM
errors, FFmpeg resampler draining at EOF, checked timestamp arithmetic,
container-start playback initialization, hardened sink snapshots, and
idempotent movie-audio shutdown. End-to-end acceptance then exposed libwebrtc's
default mono Opus negotiation; a regression assertion was added before the
movie-audio media section was changed to negotiate stereo in both offer and
answer. The final real smoke confirmed two-channel decoded PCM.

## Not verified

- audible speaker playback, remote render-time synchronization, or hard resync
- volume UI, pause, seek, subtitles, local movie rendering, or product facade
- hardware H.264, GPU zero-copy, adaptation, reconnect, or endurance
- two physical computers, TURN, or public-network connectivity
- Windows native Qt/FFmpeg/libwebrtc build, devices, or process audio capture

This stage proves independent movie PCM/Opus transport and sender-side timeline
alignment on the recorded Mac environment. It does not prove complete player
UX, audible output quality, network fallback, or Windows native media support.
