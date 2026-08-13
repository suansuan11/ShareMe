# macOS Automatic Screen Capture Recovery Verification

## Outcome

`verified-controlled-automatic-recovery-policy`

ShareMe now classifies a macOS runtime `screen-capture-stopped-*` category as
recoverable, keeps the existing call and voice path alive, and retries the same
screen source at most three times after 250, 500, and 1000 milliseconds. The
controlled native acceptance route entered this automatic policy, recreated
ScreenCaptureKit once, and recovered host and viewer video in one observation
sample while H.264 VideoToolbox and bidirectional synthetic voice remained
active.

The successful run deliberately uses a private trigger rather than physically
forcing `SCStreamDelegate::didStopWithError`. It verifies policy entry,
same-source stop/start, media recovery, counters, UI status ordering, and
cleanup. A real unsolicited ScreenCaptureKit delegate failure remains
environment-dependent. Source review plus a deterministic generation-gate test
prove that events tagged with a retired native generation are rejected; this is
not described as an Objective-C delegate integration test.

## Environment and identity

- Date: 2026-08-13
- Platform: macOS 26.6.1 (25G76), arm64
- Hardware: MacBook Air Mac16,12, Apple M4, 16 GB
- Base: `main` at `32067f8`
- Reviewed implementation: `4cfb29c`
- Demo SHA-256:
  `3089f48904068fe436fa929106124ca7520d607d261bcacd29402a497b868235`
- Motion fixture SHA-256:
  `5d01d0b55069fcf859b92b8fabfc7bd8ad529a522d596c2d8c0de3532a520435`
- Accepted JSONL SHA-256:
  `8eff6eedd9850e0b7b43a104ce774e66403fb8462acf96532be3f7e9dabf68c4`

The ignored artifact is
`out/macos-automatic-capture-recovery/standard-60s-generation-guarded.jsonl`.
The external locked libwebrtc root was consumed read-only. No generated output,
cache, path, room, token, SDP, ICE, process identifier, or child log is committed.

## Delivered contract

- A Qt-free `ScreenCaptureRecoveryPolicy` permits one active episode and
  exactly three attempts with fixed 250/500/1000 ms delays.
- Only `screen-capture-stopped-*` is recoverable. Initial start failures,
  missing dependencies, permission and other capture categories remain
  terminal under their existing behavior.
- On macOS, the late-error monitor, the peer failure callback, the initial
  peer-wait result, and the controlled probe converge on the same controller
  entry point. Generic peer/call errors cannot overwrite a recognized recovery.
- Recovery retains the same `ScreenVideoSource`, PeerConnection, video track,
  codec selection, signaling, microphone, speaker, and voice tracks.
- Attempt, success, and generation counters stay monotonic. Three failed
  attempts enter `call-error: screen-capture-recovery-exhausted` and the
  existing retryable result page.
- The call page stays visible during recovery and reports “正在恢复屏幕共享”.
- Native sample/error callbacks carry a monotonically increasing stream
  generation. The event gate rejects retired generations before they can alter
  the current stream; shutdown and failed startup retire their generation.
- Automatic classification and the acceptance trigger are both compile-time
  macOS guarded. Windows/default behavior is unchanged by this stage.

## Accepted controlled native run

Configuration: standard profile, native Cocoa host/viewer, 60 seconds, dynamic
motion fixture, policy trigger after 15 seconds, fixture pause for three
seconds, hardware required.

| Signal | Host | Viewer |
| --- | ---: | ---: |
| encoded / decoded | 3345 | 3339 |
| callback | 3380 | 3376 |
| submitted | 3380 | 3370 |
| voice packets sent | 2910 | 2938 |
| voice packets received | 2943 | 2905 |
| voice bytes sent | 235814 | 235409 |
| voice bytes received | 235814 | 235409 |
| final bitrate (bps) | 1927816 | 2447471 |
| maximum full-call stall samples | 0 | 0 |

- H.264 negotiated and VideoToolbox stayed active.
- Geometry matched at 1470x956.
- Host restart attempt/success/generation changed exactly `0/0/0 -> 1/1/1`.
- Recovery status preceded the successful restart marker.
- Capture restart transition: one sample.
- Host and viewer post-resume recovery: one sample each.
- Post-recovery observation: 42 samples.
- Viewer presentation recovery: exactly one, followed by 3268 submissions.
- Bidirectional synthetic voice satisfied the bounded continuity gate.
- The owned fixture started, remained alive, resumed, and stopped in cleanup.

## Regression evidence

- Fresh isolated `call-dev` configure/build: passed.
- Full CTest after implementation: 52/52 passed.
- `signaled_peer`: 20/20 consecutive repetitions passed.
- Screen smoke suite under system and Homebrew Python: 23/23 each.
- Windows screen contract under system and Homebrew Python: 8/8 each. This is
  portable contract evidence, not Windows native execution.
- Go `test -race ./...` and `go vet ./...`: passed.
- Sol-Terra workflow: 8/8 passed.
- ShareMe skill validator: passed.
- Portable-core forbidden-header scan: empty.
- Artifact redaction and `git diff --check`: passed.
- Independent read-only review found three Important issues: missing native
  generation identity, early peer-failure routing conflict, and overclaiming of
  the physical delegate path. The first two were fixed in `4cfb29c`; this
  document explicitly closes the evidence-boundary issue without claiming a
  physical delegate injection.

## Evidence boundaries

- **Verified on macOS:** deterministic policy bounds; macOS-only routing of
  recognized categories from controller paths; controlled policy entry;
  same-source native stop/start; H.264 VideoToolbox; matching geometry;
  host/viewer video recovery; synthetic voice continuity; UI status ordering;
  generation-gate semantics; presentation recovery; guarded cleanup.
- **Partial:** native Objective-C code associates delegate callbacks with a
  generation and was compiled/run as part of the controlled restart, but the
  accepted run did not originate from `didStopWithError`.
- **Environment-dependent:** physical unsolicited ScreenCaptureKit errors,
  sleep/wake, lock, permission revocation, display removal, audible voice,
  physical scanout, subjective quality, thermals, Windows native rerun, and 4K.
- **Unimplemented:** cursor/display selection completion, system audio, HDR,
  remote input, TURN, file sharing, and 4K60 optimization.

The next Mac stage should add a controlled native delegate fault seam or perform
an authorized physical fault campaign before promoting unsolicited
`didStopWithError` recovery from partial/environment-dependent to verified.
