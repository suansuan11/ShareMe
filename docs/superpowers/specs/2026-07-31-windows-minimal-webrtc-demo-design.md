# Windows Minimal WebRTC Demo Design

## Goal

Deliver a repeatable Windows sender/receiver demonstration that negotiates two
native libwebrtc PeerConnections through the existing local signaling service,
transmits the synthetic video track, and renders the receiver's remote frames
in a minimal Qt window. This slice establishes the Windows media baseline
before Desktop Duplication capture is introduced.

## Scope

This slice fixes the Windows loopback failure, validates the existing
Offer/Answer/ICE implementation in two processes, makes the smoke runner work
on Windows, exposes received video frames through a narrow WebRTC adapter
callback, and adds a small Qt demo for creating or joining a room and viewing
remote video.

It does not change the locked WebRTC revision or dependency bootstrap design.
It excludes Desktop Duplication, WASAPI, production room UI, TURN acceptance,
accounts, multi-party calls, codec tuning, and hardware performance claims.

## Existing Architecture

`WebRtcRuntime` owns the libwebrtc network, worker, and signaling threads plus
the PeerConnectionFactory. `LoopbackSignaling` negotiates two in-process peers.
`SignaledPeer` owns one process-local PeerConnection and exchanges opaque SDP
and ICE through callbacks. `QtSignalingClient` maps those callbacks to the v1
WebSocket protocol, and the Go service relays messages between one host and one
viewer.

The media path already creates a bounded synthetic I420 video source and
attaches received video to `CountingVideoSink`. The missing product-facing
boundary is a received-frame callback suitable for a renderer; Qt currently
renders only locally decoded FFmpeg frames.

## Windows Runtime Fix

On Windows, `WebRtcRuntime` will own a `webrtc::WinsockInitializer` for the
entire lifetime of its socket server and PeerConnections. It is created before
the network thread and destroyed after the network thread stops. A nonzero
Winsock initialization result makes runtime creation fail immediately instead
of allowing negotiation to time out without candidates. Non-Windows behavior
is unchanged.

The existing `loopback_signaling` and `webrtc_loopback` tests are the regression
tests: both currently time out because no host ICE candidate is produced. A
passing run must report connected ICE/DTLS, received video and audio, and a
selected host candidate.

## Remote Video Boundary

`SignaledPeerConfig` will accept an optional, transport-neutral remote video
frame callback. `SignaledPeer` will attach a focused sink to the remote video
track, continue collecting the existing counters, and invoke the callback with
an immutable `webrtc::VideoFrame`. Callback shutdown is explicit: callbacks are
disabled, the sink is removed on the signaling thread, queued UI deliveries use
Qt object lifetime guards, and only then are tracks and runtime threads released.

The callback remains in the WebRTC adapter contract rather than exposing a Qt
type. The Qt layer converts I420 frames to a detached image owned by the queued
UI event. No WebRTC frame storage is retained across callback boundaries.

## Minimal Qt Demo

A new `shareme_rtc_demo` target will reuse `QtSignalingClient` and
`SignaledPeer`. Command-line role selection keeps the interface intentionally
small:

- host: connect to the local server, create a room, print and display the room
  code, then send the synthetic test video;
- viewer: connect with a supplied room code, receive the test video, and render
  it in a Qt `VideoOutput`;
- both roles: display connection/failure status and stop cleanly when the
  window closes.

The host begins negotiation after `participant-joined`. The viewer starts its
peer after `room-joined`, applies the offer, returns the answer, and both peers
relay candidates through the existing protocol. The signaling server never
parses SDP, ICE addresses, or media.

## Smoke Runner

The existing two-process smoke runner will preserve POSIX process-group cleanup
and add a Windows process-tree cleanup path using `CREATE_NEW_PROCESS_GROUP`,
graceful termination, bounded waits, and final process termination. It will not
use shell-expanded commands or log SDP, candidates, tokens, or device data.

The automated smoke remains headless and validates received frame counts and
RTP statistics. The Qt demo is the visual acceptance path and reuses the same
controller rather than introducing a second negotiation implementation.

## Error Handling

- Winsock initialization failure is reported as WebRTC runtime creation failure.
- Invalid role, room code, SDP, or ICE retains the existing stable categories.
- Connection and media waits remain bounded and exit nonzero on timeout.
- Qt conversion or renderer shutdown drops the affected frame instead of
  blocking WebRTC threads or growing an unbounded queue.
- Cleanup is idempotent and releases sinks and PeerConnection proxies before
  stopping runtime threads.

## Verification

1. Configure and build against the existing locked WebRTC installation without
   running the bootstrap or downloading dependencies.
2. Run the focused `loopback_signaling` and `webrtc_loopback` tests on Windows.
3. Run the complete libwebrtc-enabled CTest suite.
4. Configure and build the Qt + WebRTC targets with the installed Qt toolchain.
5. Run the Windows two-process signaling smoke test and require connected peers,
   received video, audio RTP, and a selected host candidate on both processes.
6. Launch the host and viewer Qt demos and visually verify that the viewer
   renders the changing synthetic test pattern.

Windows claims are limited to the exact commands and outputs recorded after
implementation. Two-computer, TURN, 1080p60 performance, Desktop Duplication,
and hardware encoding remain unverified.

