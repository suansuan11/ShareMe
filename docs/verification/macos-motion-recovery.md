# macOS Screen Capture Restart Recovery Verification

## Outcome

`verified-macos-native-capture-restart-recovery`

An active macOS screen-sharing call destroyed and recreated its native
ScreenCaptureKit stream once while preserving the existing WebRTC peer,
VideoToolbox H.264 path, bidirectional synthetic voice, geometry, bitrate,
bounded queues, and viewer presentation lifecycle. Host and viewer video
advanced one observation sample after the recovery boundary, against a frozen
maximum of five.

This is a controlled restart probe, not yet automatic recovery from an
unsolicited `SCStreamDelegate` error.

## Environment and identity

- Date: 2026-08-12
- Platform: macOS 26.6.1 (25G76), arm64
- Hardware: Apple M4
- Base: `main` at `0c5ba21`
- Branch: `codex/macos-motion-recovery`
- Demo SHA-256:
  `0afb022339adf0d07d41dc7e985b09f9389763cab77d9b62321b20732f482052`
- Motion fixture SHA-256:
  `64e047f4f1c315540a8a64fd9e8d952318493d13a07388b1461173533eca391c`
- Accepted JSONL SHA-256:
  `599d4c4a2ce670a771e97bf23741c440b782968802017fdd61285908ba2202f3`

The external libwebrtc root was consumed read-only. No file beneath it was
modified during the stage; generated build trees and JSONL stayed in ignored
repository paths.

## Diagnostic progression

### Static content is not a restart signal

The first native 60-second run paused the owned fixture for three seconds but
did not recreate capture. It completed with zero video counter stalls. A second
run placed both peers offscreen while keeping the fixture native and produced
the same result. ScreenCaptureKit can deliver frames for static visible content,
so a counter plateau is not a truthful prerequisite for recovery.

Those artifacts are diagnostic only. They do not prove capture restart.

### Async voice snapshot reuse

The first generation-aware final run correctly changed host restart counters
from `0/0/0` to `1/1/1`, but the runner rejected one repeated host voice stats
snapshot. The performance output timer and media-stats worker both run at one
second and are not phase-locked: the preceding report advanced by two seconds'
worth of voice traffic and the following report reused that snapshot once.

The corrected frozen voice gate permits at most one adjacent repeated snapshot,
forbids regression or two consecutive repeats, and over the three-second probe
requires every packet counter to grow by at least 75 and every byte counter by
at least 6000. Synthetic voice nominally advances around 50 packets per second,
so this handles observation jitter without treating a final nonzero total as
continuity.

## Implemented contract

- The host controller retains the same ref-counted `ScreenVideoSource` supplied
  to the WebRTC peer factory.
- At the measured boundary, the runner first pauses the fixture, then creates a
  one-use file in a private temporary directory. The macOS-only controller
  poller acknowledges that shared trigger by invoking `stop()` and `start()` on
  the source once. The runner does not resume the fixture until the next host
  counter explicitly reports attempt/success/generation `1/1/1`. Missing
  acknowledgement at the configured resume boundary is a hard failure that
  immediately resumes and cleans up the fixture. Interruption duration is
  restricted to 3--5 seconds so the once-per-second counter cannot race a
  one-second acknowledgement window. This tears
  down and recreates `SCStream`; it does not recreate signaling, the peer
  connection, H.264 selection, or voice tracks.
- Performance output records restart attempts, successes, and generation.
- The runner also pauses/resumes its owned dynamic fixture, records role-aligned
  counter boundaries, and resumes a stopped fixture before teardown.
- Acceptance requires `0/0/0` at the initial boundary and exactly `1/1/1`
  before the resume boundary, plus video recovery within five samples,
  continuous bounded voice evidence, and at least ten post-recovery samples.
- Missing, duplicated, late, regressing, malformed, or out-of-range evidence is
  a categorized failure.

## Accepted native run

Configuration: standard profile, 60 seconds, capture restart after 15 seconds,
fixture pause from 15 through 18 seconds, native Cocoa peers.

| Signal | Host | Viewer |
| --- | ---: | ---: |
| encoded / decoded | 3311 | 3298 |
| callback | 3373 | 3331 |
| submitted | 3372 | 3324 |
| voice packets sent | 2910 | 2932 |
| voice packets received | 2943 | 2899 |
| voice bytes sent | 235814 | 234923 |
| voice bytes received | 235814 | 234923 |
| final bitrate (bps) | 128672 | 126534 |
| maximum full-call stall samples | 1 | 0 |

- H.264 negotiated and VideoToolbox stayed active.
- Host/viewer geometry matched at 1470x956.
- At host sample 13, restart attempt/success/generation were `0/0/0`; before
  the sample-16 resume boundary they were acknowledged as `1/1/1`.
- Capture restart transition: one sample.
- Host post-resume video recovery: one sample.
- Viewer post-resume video recovery: one sample.
- Post-resume observation: 42 samples.
- Viewer bounded presentation recovery: exactly one, with 3229 later
  submissions.
- Owned fixture started, stayed alive, resumed, and stopped during cleanup.
- JSONL contained no absolute user path, room, token, SDP, ICE, server address,
  process ID, or child output.

## Regression evidence

- Fresh `call-dev` configure/build: passed.
- Full `call-dev` CTest after implementation: 51/51 passed.
- `signaled_peer`: 20/20 consecutive fresh repetitions passed.
- Affected Python suites under system Python: 45 passed, one configured Windows
  fixture test skipped on macOS.
- Same affected suites under Homebrew Python: 45 passed, same skip.
- Focused final runner suite after boundary hardening: 22/22 under each Python.
- `go test -race ./...`: passed.
- `go vet ./...`: passed.
- Sol-Terra workflow: 8/8 passed.
- ShareMe skill validator: passed.
- Portable-core forbidden-header scan: empty.
- `git diff --check`: passed.

## Evidence boundaries

- **Verified on macOS:** one controlled native ScreenCaptureKit stop/start on an
  unchanged WebRTC track, restart generation evidence, VideoToolbox H.264,
  matching geometry, host/viewer recovery, synthetic voice continuity,
  presentation recovery, and guarded cleanup/redaction.
- **Partial:** this establishes the reusable source lifecycle and evidence gate,
  but not policy for retrying an unsolicited native error.
- **Environment-dependent:** real minimize/restore, occlusion behavior, screen
  lock, display sleep/wake, permission revocation, physical scanout, audible
  voice, subjective image quality, thermal observation, Windows rerun, and 4K.
- **Unimplemented:** automatic bounded restart after
  `screen-capture-stopped-*`, cursor/display selection completion, system audio,
  HDR, remote input, TURN, file sharing, and 4K60 optimization.
