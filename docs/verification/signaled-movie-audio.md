# Signaled Movie Audio Verification

## Verified scope

On 2026-07-31, a macOS ARM64 host opened one generated movie through
independent FFmpeg video-only and audio-only decoders. The decoders shared one
monotonic `MovieTimeline`: video was converted to I420 for the `movie-video`
track, while audio was normalized to 48 kHz stereo S16, rechunked to exact
10 ms blocks, and sent through an independent unprocessed `movie-audio` Opus
track. Host and viewer microphone voice tracks remained separate with native
AEC, noise suppression, and automatic gain control enabled.

The viewer never received the movie path. Structured offer parsing records
whether a `movie-audio` track is expected and its media-section MID; acceptance
cannot be bypassed when the offer declares that track but `OnTrack` never
arrives. The viewer attaches a counting sink only after the negotiated
connection has had a bounded 100 ms format-stabilization window. Every accepted
callback must contain non-null 16-bit, 48 kHz, two-channel PCM with exactly 480
frames. Invalid callbacks are counted separately, do not advance the
100-callback threshold or peak, and must remain zero in the real smoke.
Speaker playout remains disabled. The host reported generated chunks and the
absolute difference between the latest emitted movie-audio and movie-video PTS
values.

Voice RTP counters are track-specific. Outbound stats follow
`media_source_id` to `RTCAudioSourceStats.track_identifier`; inbound stats use
the identifier when it represents the SDP track and otherwise map
`RTCInboundRtpStreamStats.mid` to the structurally parsed remote voice MID.
Thus host `host-voice`/viewer `viewer-voice` packets count as voice while
`movie-audio` packets cannot inflate bidirectional voice acceptance.
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
  --port 18134 \
  --audio synthetic \
  --video synthetic

python3 scripts/run_signaled_call_smoke.py \
  --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server \
  --port 18135 \
  --audio microphone \
  --video movie \
  --movie-audio \
  --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
```

`--movie-audio` is valid only for a host using `--video movie --movie <path>`.
Viewer, missing-path, and non-movie combinations return exit code 2 without
printing the path. A call-only binary given the otherwise valid combination
prints exactly `PEER_ERROR movie-audio-dependency-unavailable` and exits 1.
Movie video without `--movie-audio` remains supported. These contracts run as
`signaled_call_cli_contract` in both call-only and movie-call CTest presets.

The smoke orchestrator waits a bounded interval for the host room line instead
of blocking on `readline()`. Server, host, and viewer each start in a new
process session. Every exit path terminates the complete process group, waits a
bounded grace period, and escalates to `SIGKILL`. The
`signaled_call_smoke_contract` test uses a fake host with no room output and an
uncooperative descendant to verify both the deadline and descendant cleanup.
Before starting Go, the script bind-probes the requested loopback port and
rejects an occupied address instead of accepting an unrelated existing health
endpoint. During health polling it also checks that the server process started
by this run is still alive. Server output goes to a non-blocking temporary file;
a bounded tail is retained only on the local startup exception object while
the displayed error category stays concise. Contract tests cover an occupied
fake health endpoint and a server that exits before becoming healthy. At the
subprocess boundary both cases exit 1 with the exact single stderr line
`SMOKE_ERROR signaling-startup-failed`; no traceback, address, workspace path,
movie path, or captured server log is printed. Other expected orchestration
failures use `SMOKE_ERROR smoke-failed`. `KeyboardInterrupt`, `SystemExit`, and
unexpected programming exceptions are deliberately not swallowed.
An unavailable optional A/V skew is printed as numeric sentinel `-1`; movie
audio acceptance rejects that sentinel before applying the 50 ms threshold.
The stable result line also reports
`movie_audio_invalid_frames_received`; movie acceptance requires it to be zero.

## Recorded results

All verification commands above exited zero. CTest passed 6/6 portable-core
tests, 19/19 playback tests, 13/13 call-only tests, and 35/35 combined
movie-call tests. Go race tests passed all five tested internal packages plus
the command package with no tests, and `go vet ./...` exited zero.

Synthetic two-process regression; viewer result is first:

```text
ROOM F6SXGI
RESULT connected=1 video=59 width=640 height=360 audio_sent=103 audio_received=102 audio_level=0.244148 movie_audio_frames_received=0 movie_audio_invalid_frames_received=0 sample_rate=0 channels=0 peak=0 chunks_generated=0 movie_av_skew_ms=-1 candidate=host error=
RESULT connected=1 video=59 width=640 height=360 audio_sent=102 audio_received=102 audio_level=0.244148 movie_audio_frames_received=0 movie_audio_invalid_frames_received=0 sample_rate=0 channels=0 peak=0 chunks_generated=0 movie_av_skew_ms=-1 candidate=host error=
```

Movie video, independent movie audio, and bidirectional microphones; viewer
result is first:

```text
ROOM ZEHMDW
RESULT connected=1 video=58 width=320 height=180 audio_sent=154 audio_received=101 audio_level=0.0220038 movie_audio_frames_received=303 movie_audio_invalid_frames_received=0 sample_rate=48000 channels=2 peak=3347 chunks_generated=0 movie_av_skew_ms=-1 candidate=host error=
RESULT connected=1 video=60 width=640 height=360 audio_sent=101 audio_received=101 audio_level=0.0241707 movie_audio_frames_received=0 movie_audio_invalid_frames_received=0 sample_rate=0 channels=0 peak=0 chunks_generated=200 movie_av_skew_ms=23 candidate=host error=
```

This exceeds the acceptance thresholds of 100 viewer movie-audio callbacks,
with every counted callback exactly 16-bit 48 kHz stereo and 480 frames, zero
invalid callbacks, a nonzero peak, 100 generated host chunks, at most 50 ms
sender-side A/V PTS skew, 20 received 320x180 movie frames, and nonzero
track-specific bidirectional voice RTP.

## Main delivery verification

The reviewed feature branch was merged to `main` with merge commit `391ba21`.
Fresh configuration and verification on the merged tree passed Go race tests
and vet, portable core 6/6, playback 19/19, call-only 13/13, and combined
movie-call 35/35. The synthetic two-process smoke passed. The final normal CLI
movie smoke reported 303 valid and zero invalid movie-audio callbacks at
48 kHz stereo, 58 received 320x180 movie frames, viewer voice RTP 154/101,
host voice RTP 101/101, 200 generated chunks, and 23 ms sender A/V skew.

The first merged-tree movie-smoke invocation returned the intentionally
sanitized `SMOKE_ERROR smoke-failed` without enough public detail to identify a
threshold. The same build and fixture then passed six diagnostic repetitions
and a final normal CLI repetition, with no process or listener left behind.
No code was changed to hide that non-reproduced timing/environmental event.
Remote `main` was verified at `391ba21`; the owned feature worktree and local
feature branch were removed after delivery.

## Environment and review fixes

- macOS 26.6 (25G72), Apple silicon ARM64
- CMake 4.3.3, Ninja 1.13.2, Apple Clang 21.0.0
- Qt 6.11.1 and FFmpeg 8.1.1
- Go 1.26.5; Python 3.9.6 for manual smoke and 3.14.6 for CTest contracts
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
