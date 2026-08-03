# Movie Audio Isolation Verification

Date: 2026-08-03 (Asia/Shanghai)

## Delivered behavior

- The primary `SignaledPeer` carries video, bidirectional voice, and the
  playback-control DataChannel; it no longer owns injected movie PCM.
- `MovieAudioPeer` owns a separate runtime and PeerConnection, disables ADM
  recording, and negotiates one stereo Opus audio m-line.
- Dedicated SDP and ICE use `movie-audio-session-description` and
  `movie-audio-ice-candidate` through explicit client/server allowlists.
- CLI and Qt controller lifecycles cancel, join, and stop the movie peer
  independently from the primary peer.
- Smoke validation rejects captured `codec collision` and `RaceDetected`
  diagnostics with a sanitized public error.

## Verified evidence — macOS arm64

- Full `movie-call-dev` build succeeded.
- Full CTest passed 39/39.
- `movie_audio_peer` passed 20 consecutive lifecycle/negotiation runs; the
  primary and dedicated peer pair passed 10 consecutive focused runs.
- Five consecutive real signaling calls in microphone + movie + movie-audio
  mode passed. Every viewer received at least 100 valid 48 kHz stereo movie
  callbacks and zero invalid callbacks; every host generated at least 100
  movie-audio chunks. Primary video and bidirectional voice metrics were
  nonzero.
- Go tests passed with the race detector, Go vet passed, workflow tests passed
  8/8, and the repository skill validator passed.
- `git diff --check` passed.

## Evidence boundaries

- GUI targets compile and CLI/QML contracts pass, but a human macOS GUI rerun
  after this fix remains pending.
- Receiver speaker playout is still unimplemented; the dedicated receive sink
  verifies decoded PCM only.
- Windows native movie/microphone behavior, TURN/public-network ICE, and audio
  device variations remain environment-dependent.
- The external libwebrtc cache was used read-only and was not cleaned or
  staged.
