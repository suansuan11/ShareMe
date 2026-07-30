# Qt Signaling Client Design

## Goal

Deliver the desktop-side signaling boundary needed to drive the Stage 1 Go
service from a Qt application, while keeping room policy and wire validation
portable and independently testable in `client/core`.

## Scope

This slice adds a C++20 room-signaling state machine, a Qt `QWebSocket` adapter,
and a small command-line integration client that can create and join a room
against the local Go service. It validates create/join events and opaque
session-description forwarding. The existing libwebrtc test probe remains the
source of test video and microphone capability; binding a PeerConnection to
signaling is the following slice.

## Design

`client/core/signaling` owns a transport-free `SignalingSession`. It creates
and serializes protocol-v1 envelopes, accepts decoded server events, tracks a
single room role/token/sequence, and exposes typed outbound commands and state
updates. It includes no Qt, WebSocket, or libwebrtc header.

`client/app/signaling` owns `QWebSocket`. It converts text frames to core input,
writes only core-produced envelopes, supplies a bearer token on reconnect, and
maps transport errors to sanitized client status. A Qt-only `shareme_signaling
probe` executable drives host and viewer sessions for a real local process;
it does not start a server or contain media.

The application remains optional: `SHAREME_ENABLE_QT` gains a Qt Network
dependency, while default core builds continue without Qt, FFmpeg, or WebRTC.

## Wire and lifecycle rules

The client begins disconnected. A host sends `create-room` with role `host`; a
viewer sends `join-room` with role `viewer` and invitation code. On successful
room response it stores the room ID and transient token in memory only. A
connected session forwards `session-description`, `ice-candidate`, and
`restart-ice` opaque payloads. A server error moves to a typed failed state
only when non-retryable; transient transport loss exposes reconnectable state.

The probe verifies the service contract through real WebSocket traffic but
does not log tokens, authorization headers, SDP, ICE candidates, or ICE
credentials.

## Acceptance

Core unit tests cover create/join serialization, room response state, event
handling, rejection, and opaque relay messages. When Qt is available, the
probe is built and tested against a local `go run ./cmd/signaling` process.
Default C++ tests and the Go service tests stay green. This does not claim a
native PeerConnection call, TURN fallback, or Windows media validation.
