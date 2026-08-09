# Complete GUI Verification

Status: `verified-macos-gui-and-native-screen-call`

## Delivered product surface

The RTC executable now starts as the ShareMe application when launched without
RTC command-line options. The GUI provides:

- a Calm Dark home page with create, join, recent-room, settings, and help;
- host and viewer preflight with room-code validation, quality profile,
  microphone, and speaker choices;
- a focus-stage call page with local/remote video, connection and room state,
  microphone, speaker, details, and leave controls;
- a responsive details drawer containing truthful media, encoder, presentation,
  audio-route, and synchronization diagnostics;
- friendly recovery, retry, and return-home behavior;
- a strict explicit CLI path retained for automation and diagnostics.

The application persists only one normalized recent room. It does not persist
credentials, media paths, or the development signaling address. Unsupported
live start/stop screen switching remains visibly disabled instead of being
simulated.

## Automated GUI evidence

On macOS 26.6.1 (25G76), Apple arm64, the `gui-call-smoke-v1` runner verified
six bounded probes: home, create preflight, join preflight, host call, viewer
call, and real host control actions. The action probe toggled microphone and
speaker state, opened the details drawer, left the call, and observed a clean
return to home. No QML `TypeError`, `ReferenceError`, binding loop, or component
load failure was accepted.

The offscreen idle probe collected 12 consecutive process samples over three
seconds. Mean CPU was 4.95%, maximum sampled CPU was 37.6%, and maximum RSS was
83,440 KiB. This is bounded smoke evidence, not a physical temperature or
sustained energy claim.

Ignored artifact:

- `out/complete-gui/gui-call-smoke.json`
- SHA-256: `08df07998cacd7798c9326e7d6ea0721495f9552154285fbe760ad0a5fa8b74b`

## Native two-peer media evidence

The established native screen-stream runner separately exercised the actual
macOS capture, WebRTC transport, presentation recovery, and bidirectional voice
counter chain. All runs required hardware encoding and did not allow software
fallback:

| Profile | Duration | Result | Codec / hardware | Actual host/viewer geometry |
| --- | ---: | --- | --- | --- |
| standard | 10 s | verified | H.264 / active | 1470x956 / 1470x956 |
| quality | 30 s | verified | H.264 / active | 1470x956 / 1470x956 |
| cinema | 30 s | verified | H.264 / active | 1470x956 / 1470x956 |

Every run reported nonzero host encode, viewer decode, bitrate, bidirectional
voice packets, and post-recovery viewer submissions. No quality policy,
configured resolution bound, frame-rate bound, bitrate policy, or codec gate
was reduced. The actual geometry is limited by the current physical display;
these runs therefore do not prove physical 1440p or 4K scanout.

Ignored artifact hashes:

- standard: `1f25f6b9ed86a1f65fcdc5326698e6651a31e19070c3a89e38433d50d7877f2d`
- quality: `bf9b465f45e94129ea234026b90a6ea36a938caf799d2ed1f06c15638d1f706b`
- cinema: `008eaf85bf89b27b862bb180a95852a58ae7e070eedeb9f0b5b359a04ee911c2`

## Regression evidence

- `call-dev`: 47/47 CTest passed.
- `movie-call-dev`: 72/72 CTest passed.
- `signaled_peer`: 20/20 repeated lifecycle runs passed.
- Go `test -race ./...` and `go vet ./...`: passed.
- Sol-Terra workflow: 8/8 passed.
- ShareMe skill validator and `git diff --check`: passed.
- External libwebrtc checkout and depot tools: clean and unmodified.

## Evidence boundaries

- **Verified:** macOS GUI navigation, validated create/join configuration,
  active-session ownership, real audio control wiring, leave/retry lifecycle,
  local screen preview, native H.264 two-peer screen transport, counter-level
  bidirectional voice continuity, and automated presentation recovery.
- **Partial / environment-dependent:** current-display visual integrity,
  foreground/background behavior, human speaker audibility, microphone echo
  quality, physical temperature, display scanout, and actual 1440p/4K panels.
- **Unverified:** the new GUI on Windows hardware and a two-device network.
- **Unimplemented:** live source switching, device hot-switching, remote input,
  system-audio capture, TURN/public-network acceptance, and production
  packaging/signing. File sharing and Movie Stage 2B remain postponed.
