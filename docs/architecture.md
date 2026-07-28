# ShareMe Architecture

## Product Boundary

ShareMe is a one-host, one-viewer desktop application for sharing a local movie
while keeping a bidirectional voice call active. The signaling service
coordinates peers but never receives or stores movie or voice media.

The first release targets Windows, 1080p60 SDR, H.264 hardware encoding, Opus
audio, WebRTC P2P with TURN fallback, independent volume controls, and basic
playout correction.

## Runtime Topology

```text
Host file
  -> FFmpeg demux/decode
  -> decoded video frame
       -> delayed local renderer
       -> bounded video queue -> hardware encoder -> WebRTC video track
  -> decoded stereo PCM
       -> delayed local output
       -> bounded media-audio queue -> WebRTC media audio track

Host microphone -> WebRTC APM -> host voice track -----+
                                                        +-> PeerConnection
Viewer microphone -> WebRTC APM -> viewer voice track -+

WebSocket: room lifecycle, SDP, ICE, reconnect coordination
DataChannel: playback state, playout reports, sync commands, metrics
```

Movie audio, host voice, and viewer voice are independent paths. AEC, noise
suppression, and automatic gain control apply only to microphone paths.

## Dependency Direction

```text
QML
  -> application facade
     -> portable core contracts
        <- FFmpeg adapter
        <- libwebrtc adapter
        <- encoder adapters
        <- Windows capture and device adapters
```

`client/core` contains portable C++20 state and policy. It must not include Qt,
FFmpeg, libwebrtc, GPU vendor SDK, D3D11, WASAPI, or operating-system headers.
Adapters translate native data into core-owned types. QML invokes a facade and
never owns media frames, blocks on media work, or calls libwebrtc directly.

## Component Responsibilities

| Component | Responsibility | Must not own |
| --- | --- | --- |
| `client/app` | Qt lifecycle, facade, QML presentation | media algorithms |
| `client/core/room` | role and room state machine | sockets |
| `client/core/signaling` | signaling commands and events | WebSocket library |
| `client/core/rtc` | peer-level contracts and capabilities | libwebrtc objects |
| `client/core/sync` | media timeline and correction policy | rendering API |
| `client/core/metrics` | typed samples and aggregation | UI widgets |
| `client/media` | demux, decode, encode, subtitle, PCM | room state |
| `client/capture` | fallback screen and process audio | direct-file playback |
| `client/platform` | devices, clocks, native handles | product policy |
| `server` | rooms, tokens, signaling relay, ICE config | media bytes |

## Queue Policies

Every queue has a fixed positive capacity, one owner, and observable counters.

| Path | Full-queue policy | Reason |
| --- | --- | --- |
| decoded video | drop oldest | stale video increases conversational delay |
| encoded video | drop oldest and request recovery if needed | latency is preferred over completeness |
| movie audio | reject newest during foundation; revisit from measurements | preserve the established audio clock |
| microphone audio | reject newest and record an underrun/overrun metric | never create an unbounded voice backlog |
| control messages | reject newest and surface overload | lost control must be visible |

Policy changes require measurements and a contract update before implementation.
No producer may silently allocate an unbounded replacement queue.

## Time Model

- Movie audio PTS is the media master clock.
- All core-facing positions use signed 64-bit milliseconds.
- RTP timestamp mapping belongs to the WebRTC adapter.
- The viewer reports the last rendered movie PTS every 250 ms.
- The host changes its local movie delay; voice is not delayed to match video.
- Clock samples include their source and capture time so wall-clock and media
  time are never compared implicitly.

## Thread and Object Ownership

Each adapter owns its worker threads and exposes non-blocking commands to the
facade. A component that starts a worker is responsible for stopping and
joining it.

Shutdown proceeds in this order:

1. stop accepting UI and signaling commands;
2. close data and media senders;
3. stop capture, decode, and encode producers;
4. drain or clear bounded queues according to their path contract;
5. join workers;
6. release WebRTC, device, decoder, GPU, and Qt resources;
7. destroy core state.

`stop()` is idempotent. Callbacks use weak ownership or are disconnected before
their target is destroyed.

## Capability and Failure Model

Hardware encoders report supported codec, pixel format, resolution, frame rate,
and dynamic bitrate capabilities. If the requested hardware path is unavailable,
the facade presents a typed capability error. CPU encoding is never selected
silently.

Platform errors cross adapter boundaries as:

- stable ShareMe error category;
- operation name;
- retryability;
- sanitized diagnostic text;
- native code retained for logs without exposing secrets.

## Planned Delivery Slices

1. portable foundation and contracts;
2. FFmpeg decode plus Qt playback demonstration;
3. WebRTC test video and microphone demonstration;
4. Windows process-loopback audio demonstration;
5. minimum call system;
6. direct movie pipeline;
7. audio isolation and synchronization;
8. adaptation, fallback capture, then later HDR/macOS work.
