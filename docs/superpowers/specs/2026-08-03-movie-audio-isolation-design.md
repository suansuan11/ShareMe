# Movie Audio Transport Isolation Design

Date: 2026-08-03 (Asia/Shanghai)

## Problem

The first macOS GUI acceptance exposed two defects that existing automation did
not reject:

1. the primary PeerConnection carries voice and movie-audio m-lines with the
   same Opus payload type 111 but different fmtp parameters, which violates the
   BUNDLE codec-consistency rule;
2. a custom `MovieAudioSource` pushes PCM on its decoder thread while the same
   PeerConnection AudioState also forwards AudioDeviceModule capture to every
   active audio send stream. Both producers can enter one
   `AudioSendStream::SendAudioData`, triggering libwebrtc's fatal serialized
   capture check.

The observed video, DataChannel playback state, and shared movie timeline are
valid and remain outside this repair.

## Selected architecture

Keep the existing primary `SignaledPeer` responsible for video, bidirectional
voice, playback control, and its current SDP/ICE messages. Remove movie audio
from that PeerConnection.

Add a dedicated `MovieAudioPeer` with its own PeerConnectionFactory,
AudioState, PeerConnection, SDP/ICE exchange, and lifecycle. Its host side has
one custom `movie-audio` send track; its viewer side receives that track and
attaches the existing counting sink. The dedicated PeerConnection disables ADM
recording before media starts, so only `MovieAudioSource` can produce samples
for its `AudioSendStream`. Because the connection has one audio m-line, its
Opus stereo parameters cannot collide with voice in a BUNDLE group.

Both peer connections reuse the same signaling room but use disjoint relay
message types:

- primary: `session-description`, `ice-candidate`;
- movie audio: `movie-audio-session-description`,
  `movie-audio-ice-candidate`.

The client `SignalingSession` and signaling-server handler both maintain relay
allowlists. They must admit the two movie-audio relay types while continuing to
reject unknown types; payloads remain opaque and room routing is unchanged.

## Components and interfaces

Create `shareme::rtc::MovieAudioPeer` beside `SignaledPeer` with:

- `MovieAudioPeerConfig { role, source_factory }`; the factory is required only
  for a host;
- the same description/candidate/failure callback shapes as `SignaledPeer`;
- `start()`, `receive_description()`, `receive_candidate()`, `wait()`,
  `cancel_wait()`, and idempotent `stop()`;
- a result containing connection state, received valid/invalid PCM callback
  counts, sample rate, channels, peak, generated chunk count, selected
  candidate type, and sanitized error.

`SignaledPeerConfig` no longer accepts `movie_audio_source_factory`, and the
primary peer no longer owns, enables, stops, measures, or SDP-munges a movie
audio track. Its voice behavior and public playback-control behavior remain
unchanged.

The RTC demo and signaled-call probe each own a primary peer plus an optional
dedicated movie-audio peer. A viewer creates the receive-side movie peer so it
can accept a host's secondary offer. Failure of the movie-audio peer reports a
sanitized movie-audio error but must not tear down an already connected primary
video/voice call.

## Lifecycle and threading

- Construct and destroy each peer on its own libwebrtc signaling thread using
  existing `BlockingCall` patterns.
- Call `SetAudioRecording(false)` on the dedicated PeerConnection before its
  offer/answer can start an audio send stream.
- Start and stop `MovieAudioSource` exactly once on the host. Stop and join the
  source worker before clearing the track and PeerConnection.
- The viewer never owns a local movie source.
- Candidates may arrive before the remote description and remain bounded by
  the existing candidate-staging contract.
- Primary and movie waiters are independent; application shutdown cancels and
  joins both before peer destruction.

## Failure behavior

- Reject a movie-audio host without a source factory.
- Reject a viewer configured with a local movie source.
- Keep file paths and raw SDP out of user-facing error categories.
- A dedicated movie-audio failure is reported distinctly and does not convert
  successful primary video delivery into a crash.
- Do not silently fall back to mixing movie audio into voice.

## Verification

Use TDD and require all of the following:

1. a failing regression proves the old primary offer contains colliding PT 111
   parameters or that primary config still accepts a movie source;
2. dedicated host/viewer peers negotiate one audio m-line, exchange stereo
   48 kHz PCM, and stop repeatedly without `RaceDetected`;
3. primary host/viewer peers retain bidirectional voice and video with no movie
   audio m-line;
4. signaled-call smoke with movie audio passes repeatedly and captured stderr
   contains neither `codec collision` nor `RaceDetected`;
5. RTC demo CLI/QML contracts, complete movie-call CTest, Go race/vet, and
   repository workflow gates pass;
6. macOS GUI recheck remains a manual acceptance step after automation. Windows
   native reruns remain environment-dependent.

## Out of scope

- receiver speaker playout and volume controls;
- voice/movie mixing;
- playback-report or hard-resync work;
- TURN/public-network acceptance;
- changes to the external libwebrtc cache or checkout.
