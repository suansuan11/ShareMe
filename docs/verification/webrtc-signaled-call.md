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
RESULT connected=1 video=60 audio_sent=103 audio_received=102 audio_level=0.244148 candidate=host error=
RESULT connected=1 video=61 audio_sent=102 audio_received=102 audio_level=0.244148 candidate=host error=
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
- movie tracks in the signaled call
- hardware encoding, adaptation, reconnect, or endurance

The physical microphone extension is verified separately in
[Signaled Microphone Call Verification](signaled-microphone-call.md).

## Windows/MSVC verification

On 2026-07-31, the same one-to-one path was verified on Windows with MSVC,
Qt 6.11.1, and locked WebRTC revision
`5ad58d70eea10785fab05ba4150e2fe22ecc7f97`. The WebRTC-enabled presets use a
Release consumer ABI because the locked WebRTC archive was built with
`is_debug=false`, `NDEBUG`, and the static MSVC runtime.

From a Visual Studio developer PowerShell, configure and build without
rebuilding or downloading WebRTC:

```powershell
cmake --fresh --preset call-dev `
  -DWEBRTC_ROOT=D:/Deps/shareme-webrtc `
  -DCMAKE_PREFIX_PATH=H:/QT6.11/6.11.1/msvc2022_64 `
  -DCMAKE_LINKER=D:/Deps/shareme-webrtc/checkout/src/third_party/llvm-build/Release+Asserts/bin/lld-link.exe
cmake --build --preset build-call-dev
$env:PATH = "H:\QT6.11\6.11.1\msvc2022_64\bin;$env:PATH"
python scripts/run_signaled_call_smoke.py `
  --probe build/call-dev/shareme_signaled_call_probe.exe `
  --server-root server
```

The Windows run completed Offer, Answer, trickled ICE, connected both native
PeerConnections, and reported 59-60 received 640x360 frames plus 102-103
bidirectional synthetic-audio RTP packets per peer.

### Minimal Qt sender and receiver

Start the local signaling service in terminal 1:

```powershell
$env:SHAREME_SIGNALING_ADDR = "127.0.0.1:18080"
Set-Location server
go run ./cmd/signaling
```

Set the Qt runtime path in terminals 2 and 3:

```powershell
$env:PATH = "H:\QT6.11\6.11.1\msvc2022_64\bin;$env:PATH"
```

Start the sender in terminal 2. It prints `ROOM ABCDEF` after creating a room.
Use `--source desktop` for the Windows Desktop Duplication source, or omit it
to retain the portable synthetic test pattern:

```powershell
build/call-dev/shareme_rtc_demo.exe `
  --server ws://127.0.0.1:18080/v1/ws `
  --role host `
  --source desktop
```

Use that room in terminal 3:

```powershell
build/call-dev/shareme_rtc_demo.exe `
  --server ws://127.0.0.1:18080/v1/ws `
  --role viewer `
  --room ABCDEF
```

The viewer renders remote I420 frames through `QVideoSink`. Desktop capture
implementation and Windows acceptance evidence are recorded in
[Windows Desktop Duplication Verification](windows-desktop-duplication.md).
