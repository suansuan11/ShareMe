# Sender Local Video Preview Verification

## Delivered behavior

- `SignaledPeerConfig` exposes an independent local-video callback.
- `SignaledPeer` observes its local WebRTC video track with a dedicated sink and
  detaches that sink before releasing the track.
- the Qt host routes local frames to its existing bounded `QVideoSink` path;
  the viewer continues to route remote frames;
- no movie decoder, audio path, signaling message, or timeline behavior changed.

## Evidence — macOS arm64

- `cmake --preset movie-call-dev -DWEBRTC_ROOT=<preserved-cache>` configured the
  isolated worktree without modifying the external libwebrtc cache.
- `cmake --build --preset build-movie-call-dev -j 4` passed.
- `ctest --preset test-movie-call-dev --output-on-failure` passed 39/39.
- `signaled_peer` verifies that a host local-preview callback receives a
  640x360 frame within two seconds and that normal cancellation/stop succeeds.
- `rtc_demo_cli_contract` verifies role routing: host local frames and viewer
  remote frames.
- a host/viewer GUI session using the supplied `01.mkv` established room
  `NWRYRR` without a captured RTC error. Automated tests establish the actual
  host local-track frame path; final human confirmation of the rendered movie
  content remains a visual acceptance step.

## Evidence boundaries

- **Verified:** macOS build, full automated suite, local-track frame delivery,
  role routing, and real-movie GUI connection.
- **Partial:** exact on-screen movie content requires human visual confirmation.
- **Environment-dependent:** Windows native build and GUI regression remain to
  be rerun on Windows.
