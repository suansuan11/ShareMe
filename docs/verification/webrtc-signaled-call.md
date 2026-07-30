# WebRTC Signaled Call Verification

## Verified scope

On 2026-07-30, two independent macOS ARM64 processes created native
PeerConnections and negotiated through `QtSignalingClient`, the local Go
WebSocket service, and protocol-v1 SDP/ICE envelopes. Each peer sent a 640x360
test pattern and synthetic 48 kHz audio track.

The repeatable command is:

```bash
cmake --fresh --preset call-dev \
  -DWEBRTC_ROOT=/Users/dio/Library/Caches/ShareMe/webrtc \
  -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-call-dev
python3 scripts/run_signaled_call_smoke.py \
  --probe build/call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
  --server-root server
```

Final integration result:

```text
ROOM TYMZCI
RESULT connected=1 video=60 audio_sent=103 audio_received=102 candidate=host error=
RESULT connected=1 video=61 audio_sent=102 audio_received=102 candidate=host error=
```

The script starts an isolated signaling service on port 18080, keeps the host
alive while the viewer joins, requires both processes to exit successfully,
and rejects zero video or audio counters. It never prints tokens, SDP, ICE
addresses, or credentials.

## Lifecycle regression

An initial successful media run exposed an exit-time crash. LLDB showed an
`AudioTrackProxy` attempting to post to the already stopped WebRTC signaling
queue. A second trace covered an outstanding CreateOffer callback during early
shutdown. The controller now disables callbacks first and releases the peer,
remote/local tracks, and audio source on the signaling thread before stopping
the runtime. `signaled_peer` covers immediate start/stop and exits cleanly.

## Not verified

- TURN or public-network connectivity
- two physical computers
- Windows/MSVC native WebRTC
- microphone or movie tracks in the signaled call
- hardware encoding, adaptation, reconnect, or endurance
