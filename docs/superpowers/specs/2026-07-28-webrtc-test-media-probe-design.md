# WebRTC Test Media Probe Design

## Purpose

The second Phase 0 demonstration proves that ShareMe can create a native
libwebrtc connection, send a generated video source and an Opus microphone
track, receive both tracks, and expose connection and RTP evidence without
introducing room, server, TURN, Qt, or movie-pipeline concerns.

This is a real media-path proof. DTLS-SRTP remains enabled, video is encoded and
decoded by libwebrtc, audio crosses an RTP sender and receiver, and success is
derived from received frames and WebRTC statistics. A mocked PeerConnection is
not an acceptable substitute.

## Decision

Build a headless, single-process loopback probe containing two
`PeerConnection` instances:

```text
generated I420 frames ----> host PeerConnection ==== encrypted RTP ====>
synthetic or microphone --> host PeerConnection                       viewer
                                                                      sinks
host observer <------------- in-process SDP/ICE relay --------------> observer
```

The in-process relay uses the same offer, answer, and trickled ICE operations
that the later WebSocket signaling layer will carry. It does not parse or
rewrite SDP and candidates. It exists only to remove the signaling server and
public-network variables from this technical proof.

This approach is preferred over modifying WebRTC's
`peerconnection_client` example because the example is not a ShareMe-owned
integration boundary and its desktop client is not currently supported on
macOS. A contract-only fake is rejected because it would not exercise ICE,
DTLS, RTP, codecs, the Audio Device Module, or shutdown ordering.

## Dependency Policy

The repository records one verified libwebrtc revision:

```text
5ad58d70eea10785fab05ba4150e2fe22ecc7f97
```

This was the upstream `lkgr` head selected on 2026-07-28. The lock file records
the full revision and GN arguments. Updating it is a deliberate dependency
change with a fresh Windows build and probe run.

The multi-gigabyte WebRTC checkout, `depot_tools`, GN output, static libraries,
generated headers, logs, and downloaded SDKs stay outside the ShareMe
repository. CMake configuration never downloads dependencies. A bootstrap
script:

1. validates the requested external dependency directory;
2. obtains or updates `depot_tools`;
3. fetches WebRTC and checks out the locked revision;
4. runs `gclient sync`;
5. generates a release-compatible static `webrtc` target with examples and
   tests disabled;
6. writes a local build manifest containing the revision, platform,
   architecture, include root, archive path, compile definitions, and GN
   arguments.

`FindWebRTC.cmake` consumes that manifest through `WEBRTC_ROOT`. It fails
configuration if the revision, architecture, library, or required headers do
not match. The default build remains dependency-free. A developer enables the
probe explicitly with `SHAREME_ENABLE_WEBRTC=ON`.

The initial GN configuration keeps built-in Opus and VP8 support, disables
examples and WebRTC's own tests, produces a non-component release library, and
uses the platform toolchain expected by the locked checkout. H.264 is not
enabled by this probe; hardware H.264 belongs to the direct-movie phase.

## Repository Boundaries

The implementation adds these focused areas:

```text
deps/webrtc.lock
scripts/bootstrap_webrtc.py
cmake/FindWebRTC.cmake

client/core/rtc/
  include/shareme/rtc/probe_contract.hpp
  src/probe_contract.cpp

client/rtc/webrtc/
  include/shareme/rtc/webrtc_probe.hpp
  src/webrtc_runtime.cpp
  src/loopback_signaling.cpp
  src/test_pattern_source.cpp
  src/webrtc_probe.cpp

client/tools/webrtc_probe/
  main.cpp

tests/rtc/
  probe_contract_test.cpp
  webrtc_loopback_test.cpp
```

`client/core/rtc` owns dependency-free configuration, result, status, and
validation types. It contains no libwebrtc headers or native handles.

`client/rtc/webrtc` owns all libwebrtc objects, threads, observers, media
sources, sinks, and type conversion. Native types do not cross its public
boundary.

`client/tools/webrtc_probe` parses command-line options, runs the adapter, emits
one sanitized JSON result, and chooses a process exit code. It contains no
PeerConnection behavior.

## Public Probe Contract

The dependency-free contract is:

```cpp
enum class ProbeAudioMode {
  synthetic,
  microphone,
};

enum class ProbeStatus {
  passed,
  timed_out,
  dependency_error,
  permission_denied,
  negotiation_failed,
  media_failed,
  shutdown_failed,
};

struct ProbeConfig {
  ProbeAudioMode audio_mode{ProbeAudioMode::synthetic};
  int width{640};
  int height{360};
  int frames_per_second{30};
  std::chrono::milliseconds run_for{3'000};
  std::chrono::milliseconds connect_timeout{10'000};
};

struct ProbeResult {
  ProbeStatus status;
  std::chrono::milliseconds connection_time;
  std::uint64_t video_frames_sent;
  std::uint64_t video_frames_received;
  std::uint64_t video_frames_dropped;
  std::uint64_t audio_packets_sent;
  std::uint64_t audio_packets_received;
  std::uint64_t audio_bytes_sent;
  std::uint64_t audio_bytes_received;
  std::optional<double> round_trip_time_ms;
  std::string selected_candidate_type;
  std::string diagnostic;
};
```

Validation rejects non-positive dimensions, odd I420 dimensions, frame rates
outside 1–60, run durations outside 1–30 seconds, and connection timeouts
outside 1–30 seconds.

Diagnostics use stable ShareMe categories and sanitized text. SDP, full ICE
candidates, device identifiers, local addresses, and permission details are
never emitted in normal output.

## Video Path

`TestPatternSource` is a ShareMe-owned
`AdaptedVideoTrackSource`. A dedicated libwebrtc task queue produces 640×360
I420 frames at 30 fps by default. Each frame contains moving color bars and a
frame-index pattern so a human can recognize motion.

The source submits frames directly to libwebrtc and does not maintain a second
unbounded queue. Sink wants may reduce resolution or frame rate. The source
records generated, submitted, and adaptation-dropped counters.

The host adds the resulting track with identifier `movie-video`. The viewer
attaches a counting sink to the received track. The sink validates positive
dimensions and strictly increasing render timestamps, counts received frames,
and retains no more than the latest frame metadata.

VP8 is used for this probe because it is built into libwebrtc and isolates the
PeerConnection proof from the later hardware H.264 encoder decision.

## Audio Path

The probe has two explicit modes:

- `synthetic` injects deterministic 48 kHz mono tone samples through a WebRTC
  test audio device. It is suitable for automation and requires no hardware or
  operating-system permission.
- `microphone` uses libwebrtc's native desktop Audio Device Module and default
  microphone. It requires an interactive permission grant where the platform
  demands one.

Both modes create the `host-voice` audio track through
`CreateAudioSource` and the built-in Opus encoder. The viewer receives the
track but does not play it through the default speaker, avoiding local acoustic
feedback during a single-machine loopback run.

Synthetic mode passes only when outbound and inbound audio RTP packet counts
are positive. Microphone mode additionally reports the observed audio level;
audible capture remains a manual acceptance item because packet flow alone
cannot prove microphone quality.

AEC, NS, and AGC configuration applies only to the microphone source. There is
no movie-audio track in this demonstration.

## Negotiation and Connection

The host and viewer each own a Unified Plan `PeerConnection`. No STUN or TURN
server is configured, so this proof uses local host candidates only.

The coordinator performs:

1. create both factories and PeerConnections;
2. add host video and voice tracks;
3. create and set the host offer;
4. set the offer on the viewer;
5. create and set the viewer answer;
6. set the answer on the host;
7. forward every trickled ICE candidate to the opposite peer;
8. wait for connected/completed ICE and connected DTLS;
9. run media for the configured interval;
10. collect final standard WebRTC statistics;
11. close both peers and release media and runtime resources.

ICE candidates arriving before the opposite remote description are stored in a
fixed queue of 64 entries per peer. Overflow fails the probe visibly. After the
remote description is set, queued candidates are applied in arrival order and
new candidates are forwarded directly.

The probe never disables encryption and never accepts success from signaling
state alone.

## Threads and Lifetime

`WebRtcRuntime` owns one network thread, one worker thread, and one signaling
thread. The probe coordinator is serialized on the signaling thread.
`TestPatternSource` owns one task queue for scheduled frame generation.

Shutdown is idempotent and ordered:

1. stop accepting coordinator operations;
2. stop synthetic/native recording and the video source;
3. detach receiver sinks;
4. close host and viewer PeerConnections;
5. release tracks, sources, factories, and observers;
6. stop and join signaling, worker, and network threads;
7. publish the final result.

Callbacks hold weak state and become no-ops after shutdown begins. Timeouts
initiate the same shutdown path. No task may outlive the runtime that created
it.

## Success Criteria and Evidence

The automated synthetic run passes only when:

- both PeerConnections reach connected/completed ICE and connected DTLS within
  10 seconds;
- at least 30 valid video frames arrive during a 3-second run;
- received video timestamps are increasing;
- outbound and inbound audio RTP packet and byte counts are positive;
- no candidate queue overflow, media error, or shutdown error occurs;
- the process exits within 15 seconds.

The executable prints one JSON object containing the locked revision, platform,
architecture, timing, media counters, optional RTT, selected candidate type,
status, and sanitized diagnostic.

The real microphone acceptance run uses `--audio microphone --seconds 10`.
The operator grants permission, speaks, verifies a non-zero reported audio
level and positive audio RTP counters, and records the platform result.

Windows and macOS results are reported separately. A macOS result does not
prove Windows microphone permission, device handling, or toolchain
compatibility.

## Testing Strategy

Development follows red-green-refactor:

1. dependency-free tests cover configuration validation, status serialization,
   and bounded candidate staging policy;
2. adapter tests cover generated-frame contents, frame pacing, observer state
   transitions, timeout handling, and idempotent shutdown;
3. a real synthetic loopback CTest exercises ICE, DTLS, VP8, Opus, frame
   reception, RTP statistics, and shutdown;
4. a manual microphone run verifies the physical device and permission path;
5. the existing default, FFmpeg, and Qt presets remain green with WebRTC
   disabled.

WebRTC tests are registered only when the locked external dependency is
available and `SHAREME_ENABLE_WEBRTC=ON`. Core CI continues to prove the
dependency-free build. A WebRTC-enabled CI job is added only after a
reproducible cached dependency build is available; absence of that job is
reported rather than treated as WebRTC verification.

## Error Handling

Configuration errors fail before any thread starts. Runtime failures map to one
stable `ProbeStatus` and preserve the first diagnostic. Later cleanup errors
are counted and may upgrade an otherwise successful result to
`shutdown_failed`.

Microphone permission denial maps to `permission_denied`. Missing or mismatched
external dependencies map to `dependency_error`. Offer/answer, remote
description, candidate, ICE, or DTLS failures map to
`negotiation_failed`. Missing frames or RTP evidence maps to `media_failed`.
Timeout always returns `timed_out`.

Every failure executes the normal shutdown sequence. The CLI returns zero only
for `passed`.

## Non-Goals

This slice does not implement:

- Qt room or call UI;
- WebSocket signaling or protocol serialization;
- public STUN/TURN traversal;
- reconnect, ICE restart, or network switching;
- DataChannel control messages;
- movie frames or movie audio;
- H.264 or hardware encoding;
- remote audio playback quality;
- two-machine or public-network validation.

Those capabilities remain in Phase 1 and later delivery slices.
