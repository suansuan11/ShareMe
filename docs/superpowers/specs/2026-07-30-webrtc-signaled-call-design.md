# WebRTC Signaled Test Call Design

## Goal

Connect two native libwebrtc peer connections through the existing Qt and Go
signaling path, proving a real local test-video and bidirectional synthetic
audio call rather than an in-process loopback.

## Scope

This slice adds a native call controller under `client/rtc/webrtc` and extends
the Qt signaling probe into host/viewer call modes. The host creates the room,
the viewer joins with its code, and each side forwards offer/answer/ICE payloads
through `QtSignalingClient`. The call succeeds only after both peers report a
connected selected candidate pair and received test media.

It excludes movie media, production microphone/device UI, TURN deployment,
reconnect UI, and cross-machine/Windows acceptance.

## Architecture

The controller owns `WebRtcRuntime`, one `PeerConnection`, test video source,
synthetic audio device, and all libwebrtc observer callbacks. It accepts a
transport-neutral outbound callback and a method for inbound signaling JSON.
Qt remains only a transport adapter; it forwards opaque SDP and ICE payloads
and never owns native WebRTC objects. PeerConnection lifetime remains bound to
the libwebrtc signaling thread and uses the existing shutdown hook discipline.

The host creates an offer after `participant-joined`; the viewer applies it,
creates an answer, and both stage ICE until remote descriptions exist. Candidate
payloads use the current v1 signaling envelope without parsing network
addresses in Qt or Go.

## Acceptance

Automated native unit tests cover controller input validation and ICE staging.
The macOS integration command runs host and viewer probe processes against a
local Go signaling service and asserts connection, selected candidate, received
video frames, and inbound/outbound audio packets on both sides. A failed
connection, timeout, or missing received media exits nonzero without logging
tokens, SDP, candidate addresses, or credentials.

This is a local host-candidate test. It is not evidence of TURN, public-network,
Windows, microphone, movie, or hardware-encoder support.
