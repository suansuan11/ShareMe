# Receiver Playout Reports Verification

## Delivered behavior

- movie video exposes an atomic same-frame PTS/RTP/generation sample;
- `playback-state` carries a distinct current host PTS and same-generation video
  anchor;
- the viewer maps frames submitted to Qt from 90 kHz RTP time to absolute movie
  PTS, including 32-bit wrap handling;
- a generation-aware tracker rejects pre-anchor, regressing, and old-generation
  rendered samples;
- the viewer sends validated `playout-report` messages at most every 250 ms;
- the host rejects wrong-generation or non-increasing reports and exposes the
  observed PTS delta plus the existing `SyncController` decision;
- no correction, rate change, buffer mutation, or hard-resync command is
  applied.

## Automated evidence — macOS arm64

- the movie-call development build completes against the preserved external
  libwebrtc cache;
- CTest passes 40/40, including new `playout_report` coverage;
- the `signaled_peer` integration test proves the reliable ordered DataChannel
  carries both host playback-state and viewer playout-report payloads;
- protocol tests cover strict room/version/type/integer/buffer validation,
  forward and reverse RTP wrap, the ten-second anchor window, report sequence,
  and generation gates;
- movie video tests prove the emitted anchor changes to generation 1 and 2
  after forward and backward seeks while transport timestamps stay monotonic;
- the RTC demo contract verifies report publication, host decoding, generation
  tracking, sync-decision observation, and QML telemetry properties.

## Real-media evidence — macOS arm64

A host/viewer GUI session using the supplied `01.mkv` remained connected in
room `MQZEEQ` for approximately ten seconds without a captured RTC, decode, or
audio error. The desktop test interface could not address the ad-hoc unbundled
Qt processes by application identifier, so exact on-screen report values remain
a human acceptance step rather than an automated claim.

## Evidence boundaries

- **Verified:** protocol/reconciliation logic, movie anchor generation, native
  build/tests, bidirectional control transport, role wiring, and real-media call
  stability on macOS.
- **Partial:** visible telemetry values and long-run drift distribution require
  human or instrumented acceptance; Qt sink submission is not display scanout.
- **Environment-dependent:** Windows native build, speaker, video, and telemetry
  reruns remain required.
- **Unimplemented:** correction application and bounded hard resync.
