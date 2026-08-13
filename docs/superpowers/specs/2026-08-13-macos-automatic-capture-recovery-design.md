# macOS Automatic Screen Capture Recovery Design

## Outcome

When an active macOS host reports an unsolicited
`screen-capture-stopped-*` error, ShareMe keeps signaling, the existing
PeerConnection, the video track, and bidirectional voice alive while it makes
at most three bounded attempts to restart the same `ScreenVideoSource`.
Successful recovery returns the call to `connected`; exhausted recovery enters
the existing retryable call-result flow with a sanitized category.

This stage does not change resolution, frame rate, bitrate, encoder selection,
queue bounds, video adaptation, or audio routing. Windows behavior remains
unchanged.

## Selected architecture

The RTC demo controller owns orchestration because it already observes late
video-source errors, owns the retained screen source, exposes call status to
QML, and controls peer shutdown. A small Qt-free policy type owns only the
deterministic recovery state:

- inactive, waiting, attempting, recovered, or exhausted;
- maximum three attempts;
- retry delays of 250, 500, and 1000 milliseconds;
- one terminal success or exhausted result per recovery episode.

The policy never touches ScreenCaptureKit, WebRTC, Qt, or UI. The controller
uses one owned `QTimer` to execute the policy decision and invokes `stop()` then
`start()` on the same ref-counted source. It stops error polling while recovery
is active, preventing overlapping episodes. Shutdown always stops both the
error monitor and retry timer before releasing the source.

Backend-owned retry was rejected because it cannot coordinate call status,
retry UX, or peer shutdown. Rebuilding the PeerConnection was rejected because
it would interrupt voice and renegotiate otherwise healthy media.

## Error and retry contract

Only a runtime error beginning with `screen-capture-stopped-` is automatically
recoverable. Missing sources, initial startup failure, unsupported platform,
and other capture categories preserve the existing terminal failure behavior.

For a recoverable episode:

1. Stop the 250 ms late-error monitor and begin one policy episode.
2. Publish `screen-capture-recovering:<attempt>` before each attempt.
3. After the corresponding 250, 500, or 1000 ms delay, stop and start the same
   `ScreenVideoSource`.
4. On success, increment restart success and generation exactly once, publish a
   sanitized smoke marker, return status to `connected`, and restart the error
   monitor.
5. On failure, ask the policy for the next bounded delay.
6. After attempt three fails, publish
   `call-error: screen-capture-recovery-exhausted`, leave error monitoring
   stopped, and let the existing application recovery dialog offer Retry or
   Return Home.

The existing restart attempt/success/generation counters remain monotonic for
the whole call. A successful first attempt therefore retains the prior
`0/0/0 -> 1/1/1` evidence contract. Voice tracks and the peer are never stopped
by capture recovery.

## Controlled acceptance probe

The macOS-only private trigger-file probe remains disabled unless explicitly
configured by the smoke runner. Instead of directly restarting capture, it
enters the same controller recovery function with the sanitized synthetic
category `screen-capture-stopped-probe`. Unit tests separately prove that the
real ScreenCaptureKit delegate error is exposed through
`SignaledPeer::video_source_error()` to that same function.

The probe proves the controller state machine, timer, same-source restart,
counter transition, media recovery, voice continuity, and cleanup. It does not
claim that sleep/wake, screen lock, permission revocation, display removal, or
every native `NSError` has been physically induced.

## User-visible state

While recovery is active, the call page remains open and the last submitted
frame stays visible. The top status label reads “正在恢复屏幕共享”; host-stage copy
uses the same message instead of reverting to initial room setup. Microphone,
speaker, leave, and details controls remain available.

If all attempts fail, the existing result page displays the friendly screen
capture message and the sanitized category. No raw NSError text, path, room,
SDP, ICE, token, or process identifier reaches QML or JSONL.

## Verification and acceptance

Automated acceptance requires:

- policy RED/GREEN tests for exact delays, success, exhaustion, duplicate
  begin, and reset;
- controller/QML contract tests for one owned timer, macOS-only probe, recovery
  status, terminal `call-error`, and shutdown cancellation;
- existing source tests proving runtime error propagation and same-source
  restart;
- a 60-second native macOS standard-profile call with one controlled failure at
  15 seconds and a three-second motion-fixture pause;
- exact first-attempt `0/0/0 -> 1/1/1`, H.264 VideoToolbox, matching geometry,
  video recovery within five samples, continuous bidirectional synthetic voice,
  bounded presentation recovery, and at least ten post-recovery samples;
- full affected Python suites, full CTest, `signaled_peer` 20/20, Go race/vet,
  workflow 8/8, skill validation, portable-core scan, redaction, cache
  preservation, and `git diff --check`.

Evidence labels remain separate: controlled macOS recovery can be verified;
physical sleep/wake, screen lock, display removal, permission revocation,
audible voice, scanout, thermals, Windows native behavior, and 4K remain
environment-dependent until run on their named environments.
