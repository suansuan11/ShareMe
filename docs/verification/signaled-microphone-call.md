# Signaled Microphone Call Verification

## Verified scope

On 2026-07-30, two independent macOS ARM64 processes opened the native default
recording device, created microphone WebRTC audio sources, and negotiated
through `QtSignalingClient` and the local Go WebSocket service. Both peers sent
and received Opus RTP while continuing to exchange generated test video.

The microphone mode is explicit and never falls back to synthetic audio.
macOS permission is checked through AVFoundation before native audio-device
initialization. Creation failures are reported as one sanitized category:
`permission-denied`, `dependency-unavailable`, or
`audio-initialization-failed`. AEC, noise suppression, and automatic gain
control are enabled only for microphone sources. Remote speaker playout remains
disabled in this two-process-on-one-Mac probe to avoid acoustic feedback.

## Repeatable commands

```bash
cmake --fresh --preset call-dev \
  -DWEBRTC_ROOT=/Users/dio/Library/Caches/ShareMe/webrtc \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-call-dev

python3 scripts/run_signaled_call_smoke.py \
  --probe build/call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server \
  --port 18083 \
  --audio synthetic

python3 scripts/run_signaled_call_smoke.py \
  --probe build/call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server \
  --port 18084 \
  --audio microphone
```

The script requires nonzero received video, sent audio, and received audio for
both modes. In microphone mode it additionally requires a positive local audio
level from both peers. It prints only the short-lived room code and sanitized
result counters; tokens, SDP, candidates, ICE addresses, and credentials are
not printed.

## Recorded microphone result

```text
ROOM 7RS3KG
RESULT connected=1 video=60 audio_sent=101 audio_received=101 audio_level=0.00747703 candidate=host error=
RESULT connected=1 video=60 audio_sent=102 audio_received=101 audio_level=0.00747703 candidate=host error=
```

Environment:

- macOS 26.6 (25G72), Apple silicon ARM64
- Apple Clang through CMake 4.3.3 and Ninja
- Qt 6.11.1
- Go 1.26.5
- locked libwebrtc revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`

## Not verified

- audible remote-speaker playout or echo quality
- two physical computers or two independent microphone devices
- Windows native microphone permission and device behavior
- TURN or public-network connectivity
- movie audio, hardware encoding, reconnect, adaptation, or endurance

This result proves native microphone capture and bidirectional RTP transport on
the recorded Mac environment. It does not establish production voice quality
or Windows media support.
