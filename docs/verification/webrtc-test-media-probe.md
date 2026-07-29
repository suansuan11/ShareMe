# WebRTC Test Media Probe Verification

## Verified revision and environment

- Source revision: `4208b59ecce249a3185bc84d8933ae9edc421f56`
- Date: 2026-07-29
- Platform: macOS 26.6 (25G72), Apple Silicon ARM64
- Compiler: AppleClang 21.0.0.21000101
- CMake: 4.3.3
- libwebrtc: repository-external checkout locked by `deps/webrtc.lock.json`

The checkout, generated manifest, libraries, logs, SDP, ICE credentials,
candidate addresses, device identifiers, and permission records are not
tracked by this repository.

## Automated results

| Configuration | Result |
| --- | --- |
| dependency-free core | 5/5 passed |
| FFmpeg media | 9/9 passed |
| Qt + FFmpeg playback | 11/11 passed on final full rerun |
| libwebrtc | 9/9 passed |
| WebRTC bootstrap Python tests | 12/12 passed |
| GitHub Core CI at `4208b59` | passed on macOS and Windows default builds |

The first fresh Qt playback run had one output-free
`playback_media_smoke` failure. Its fixture and focused test immediately passed,
and a subsequent complete 11/11 run passed. No playback source changed in this
slice; the isolated result is recorded as a transient offscreen-startup
observation rather than omitted.

## Synthetic loopback

Command:

```bash
./build/webrtc-dev/client/tools/webrtc_probe/shareme_webrtc_probe \
  --audio synthetic --seconds 3
```

Result:

- status: `passed`
- connection: 13 ms
- video frames generated/received/dropped: 90 / 88 / 1
- audio packets sent/received: 150 / 150
- audio bytes sent/received: 12,254 / 12,254
- selected candidate type: `host`
- process exit code: 0

The pass condition uses received video frames, inbound and outbound audio RTP
counters, current ICE/DTLS state, and selected candidate-pair stats. A state
transition alone is insufficient.

## Microphone loopback

Command:

```bash
./build/webrtc-dev/client/tools/webrtc_probe/shareme_webrtc_probe \
  --audio microphone --seconds 10
```

Result on this Mac:

- status: `passed`
- connection: 5 ms
- video frames generated/received/dropped: 300 / 299 / 1
- audio packets sent/received: 498 / 498
- audio bytes sent/received: 34,128 / 34,128
- audio level: 0.0310068056276131
- selected candidate type: `host`
- process exit code: 0

macOS permission is checked with AVFoundation. A first explicit microphone run
requests access using the CLI's embedded `NSMicrophoneUsageDescription`; a
known denial maps to `permission-denied`. Receiver speaker playout is disabled.

## Dependency and lifecycle contract

- `client/rtc/webrtc` owns all native libwebrtc objects.
- `deps/webrtc.lock.json` is the update authority for the external checkout.
- Synthetic audio is a continuous 440 Hz, 48 kHz mono source.
- Microphone audio alone enables AEC, AGC, and noise suppression.
- ICE staging is bounded at 64 candidates per destination.
- Shutdown removes sinks, stops signaling, releases proxy objects on the
  signaling thread, and then stops worker threads and SSL.

## Not verified

- Windows/MSVC libwebrtc build or microphone behavior
- TURN or relay candidates
- two-machine or public-network connectivity
- reconnect behavior
- Qt integration with the WebRTC probe
- H.264 hardware encoding
- movie audio as a distinct WebRTC track
- 1080p60 performance, endurance, loss, or network adaptation

The Windows user machine should run the locked WebRTC build and synthetic probe
after pulling this branch. Until that run, only dependency-free Windows Core CI
is verified.
