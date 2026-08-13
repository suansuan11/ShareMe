# Primary Voice Control and Quality Diagnostics Verification

Date: 2026-08-13

## Outcome

Stage status is `partial-audible-acceptance`.

The primary bidirectional voice path now has a dedicated in-call panel for
microphone activity, remote-voice volume, the existing microphone processing
policy, and conservative transport-quality status. Movie audio remains in its
separate diagnostics section. System/application audio capture remains
unimplemented.

MotionFixture is no longer part of audio-focused automation. It remains an
explicit dependency only of gates that must prove changing screen pixels.

## Verified on macOS

Platform: macOS 26.6.1, Apple M4 arm64.

- Fresh isolated `voice-control-dev` configuration and build completed against
  the preserved locked WebRTC dependency.
- Full configured CTest passed `57/57`, including the new interval-quality and
  fixture-free voice runner contracts.
- `signaled_peer` lifecycle repetition passed `20/20`.
- The focused runner and QML suites passed under Homebrew Python and the macOS
  system Python.
- Signaling `go test -race ./...` and `go vet ./...` passed.
- Sol-Terra workflow tests passed `8/8`; the ShareMe skill validator and
  `git diff --check` passed.

The native 15-second two-peer synthetic run used standard screen capture only
as the existing call carrier. Both peers explicitly selected synthetic primary
voice and disabled native speaker playout. Results:

| Counter | Value |
| --- | ---: |
| host voice packets sent | 652 |
| host voice packets received | 658 |
| viewer voice packets sent | 653 |
| viewer voice packets received | 647 |
| MotionFixture requested | false |
| MotionFixture started | false |
| native speaker playout | false |

Sanitized artifact:
`out/primary-voice-control-quality/macos-15s-final.jsonl` (ignored, not committed),
SHA-256
`dd475e1dd247f0a960c29c0734b36eee3bd9747af2738230556f15ee33a03246`.

## Code-level contracts

- Microphone sources retain explicit echo cancellation, noise suppression, and
  automatic gain control; synthetic and movie sources remain unprocessed, and
  the UI identifies those modes accurately.
- Speaker volume maps 0-100 to native device bounds, rejects invalid values,
  and updates the UI only after the native operation succeeds.
- Voice diagnostics select the expected primary-voice RTP track and do not use
  movie-audio identifiers.
- The interval classifier treats missing data as `checking`, counter regression
  as `poor`, and remote mute as `muted`; it does not store raw audio.
- One statistics worker supplies both existing performance evidence and voice
  diagnostics. The UI timer does not create a second WebRTC stats poller.

## Partial and environment-dependent evidence

- The automated run proves bidirectional synthetic RTP and explicit fixture
  absence. It does not prove physical microphone sound, speaker audibility,
  subjective echo/noise quality, or acoustic A/V synchronization.
- Native speaker-volume mapping and failure truthfulness are covered by focused
  tests, including QML rollback to the accepted value, but a real output-device volume change was
  not performed because the safe automated run disables speaker playout.
- A two-device human pass is still required: both people must confirm
  intelligible speech, mute/unmute, volume changes, absence of persistent echo,
  and continuity while a real screen is shared.
- Windows native build/device behavior and audio hot-switching were not run in
  this macOS stage. Portable/source gates do not replace Windows evidence.
- System/application audio capture, automatic input/output device selection,
  cross-call volume persistence, and acoustic metrics are unimplemented.

## Repository and dependency hygiene

Only source, tests, scripts, specifications, plans, and verification handoff
files are intended for Git. Build trees, JSONL artifacts, logs, media, local
settings, and MotionFixture output are ignored. The external libwebrtc checkout
and cache were consumed read-only and were not cleaned, rewritten, or staged.
