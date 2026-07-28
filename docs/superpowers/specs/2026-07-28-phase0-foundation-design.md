# ShareMe Phase 0 Foundation Design

## Scope

This design covers the first independently verifiable slice of Phase 0:

- repository and build conventions;
- architecture, protocol, and ownership contracts;
- a dependency-light C++20 core;
- bounded media queue and synchronization decision contracts;
- macOS and Windows continuous integration for the portable core.

The three technical demonstrations from the main plan remain separate follow-up
slices:

1. FFmpeg decode and Qt playback;
2. WebRTC test video and microphone transport;
3. Windows process-loopback audio capture.

Keeping those demonstrations separate prevents the large Qt, FFmpeg, WebRTC,
and Windows SDK dependency surfaces from blocking the core contracts.

## Architecture

ShareMe is a modular desktop application with a portable C++ core and
platform-specific adapters. QML calls a stable application facade; it never
owns media frames or invokes WebRTC directly. FFmpeg, libwebrtc, audio devices,
and GPU codecs are adapters around contracts owned by the core.

The first slice creates two executable contracts:

- `BoundedQueue<T>` provides explicit capacity and overload behavior. Video
  producers use `drop-oldest`; control and audio paths use `reject-newest`
  unless a later measured requirement introduces a different policy.
- `SyncController` maps host and viewer media positions to a small set of
  correction actions. It is pure C++ so its thresholds and boundary behavior
  can be verified without a media runtime.

## Repository Boundaries

```text
client/
  core/        Portable state, queue, clock, synchronization, and metrics logic
  media/       FFmpeg and codec adapters
  capture/     Windows capture implementations
  app/         Qt/QML application and facade
server/        Go signaling service
deploy/        TURN, reverse proxy, and deployment configuration
tests/         Cross-component and performance tests
docs/          Architecture, protocols, contracts, and operating guidance
```

Cross-module dependencies point inward toward `client/core`. Platform adapters
may depend on the core; the core does not include Qt, FFmpeg, libwebrtc, D3D11,
WASAPI, or operating-system headers.

## Build and Dependency Policy

- CMake 3.24 or newer and C++20 are the portable baseline.
- Ninja is the default local generator.
- Project-wide presets are committed in `CMakePresets.json`;
  `CMakeUserPresets.json` is local-only.
- Tests use CTest and a small in-repository assertion harness initially, keeping
  the foundation build network-independent.
- Qt is preinstalled and discovered with `find_package` in the Qt demo slice.
- FFmpeg is linked through its documented libraries rather than shelling out to
  the `ffmpeg` executable.
- libwebrtc is treated as a versioned external SDK because its Chromium-based
  source checkout and build are too large to hide inside a normal CMake
  configure.
- Runtime secrets, media samples, dependency caches, dumps, and generated
  output are never committed.

## Protocol Direction

The signaling server exchanges room lifecycle messages, SDP descriptions, and
ICE candidates over WebSocket. Playback state and viewer playout reports travel
over the WebRTC data channel after peer connection establishment.

Every message envelope contains:

- a protocol version;
- a message type;
- a room identifier where applicable;
- a monotonically increasing sequence number;
- a payload defined in `docs/protocols.md`.

Media bytes never pass through the signaling server.

## Error and Lifecycle Rules

- Every worker and queue has one owning component.
- Stop is idempotent and joins workers before dependencies are destroyed.
- Queue capacity is fixed at construction and cannot be zero.
- Overload is observable through counters; it is never silently unbounded.
- Unsupported hardware encoding is a surfaced capability result, not a silent
  CPU fallback.
- Platform adapters return typed failures to the facade; QML displays errors
  but does not interpret native error codes.

## Verification

This slice is accepted when:

- clean configure, build, and CTest runs pass on macOS ARM64 and Windows x64;
- queue tests cover capacity, both overload policies, ordering, and counters;
- synchronization tests cover every threshold boundary from the main plan;
- architecture, protocol, and ownership documents agree on module direction,
  message ownership, queue policies, and lifecycle rules;
- CI does not require Qt, FFmpeg, libwebrtc, or media samples for the portable
  foundation job.

Windows-only media and capture claims are explicitly outside this slice until
they have run on the user's Windows machine or an equivalent Windows runner.
