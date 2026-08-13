# macOS Session Lifecycle Recovery Verification

## Result

`verified-controlled-session-lifecycle-readiness`

ShareMe now observes macOS sleep/wake and session lock/unlock without rebuilding
a healthy call. Nested lifecycle causes are folded into one generation, capture
recovery is suspended while the session is inactive or evaluating, and exactly
one post-resume decision is made after a 750 ms settle window. A healthy call
returns to `connected`; a recoverable ScreenCaptureKit error reuses the existing
bounded same-source recovery; an unavailable peer, signaling path, or media
snapshot enters the existing retryable result page.

The accepted evidence is deliberately split into controlled application-level
lifecycle events and physical operating-system behavior. The controlled runner
does not call `pmset`, AppleScript, CGSession, a password API, or any display
operation, and it does not post native macOS notifications. It therefore proves
the product policy, controller causality, media preservation, and Apple build,
but it does **not** prove that this session physically slept, woke, locked, or
unlocked.

## Delivered behavior

- A Qt-free `SessionLifecyclePolicy` rejects stale or duplicate resume events,
  folds lock plus sleep into one generation, and evaluates only when every
  active cause is cleared.
- A macOS Objective-C++ monitor owns strong notification tokens for
  `NSWorkspaceWillSleepNotification`, `NSWorkspaceDidWakeNotification`, and the
  distributed `com.apple.screenIsLocked` / `com.apple.screenIsUnlocked`
  notifications. Stop/destruction disables delivery before unregistering.
- The controller leaves signaling, PeerConnection, tracks, VideoToolbox, voice,
  current dimensions, cadence, bitrate, queues, and the last submitted frame in
  place while suspended.
- Capture-error polling and queued recovery attempts cannot restart
  ScreenCaptureKit during suspension or the settle window. A restart becomes
  legal only after the post-resume decision explicitly classifies the source as
  recoverable.
- A connection-loss decision is intentionally terminal for this stage and
  produces retryable `call-error: session-resume-connection-lost`; automatic
  signaling reconnect/rejoin is not implemented.
- The test runner records sanitized lifecycle generation, media counters, and
  completion state. It requires both peers to acknowledge the final event before
  a controlled capture fault may be injected, then bounds recovery to five
  counter samples and requires at least ten post-resume samples.
- A runner failure found during review was fixed: an extension validation error
  can no longer be finalized as success merely because the base screen runner
  had already produced a summary.

## Native controlled evidence

Environment: macOS 26.6.1 build 25G76, Apple M4, arm64. Both accepted calls used
the `quality` profile, H.264, active VideoToolbox encoding, the same
`1470x956` capture geometry at both peers, and the deterministic motion fixture.

### Nested lock/sleep/wake/unlock healthy call

Artifact: ignored
`out/macos-session-lifecycle-recovery/final-controlled-nested-60s.jsonl`

SHA-256:
`443270919595dce604e3b02fdcf0384c5127584415aa0ec4d62ae24f1f103eed`

- Both roles observed `screen-locked -> will-sleep -> did-wake ->
  screen-unlocked` in the same lifecycle generation.
- The post-resume decision was healthy, so capture restart count remained zero.
- Host encoded 3311 frames and submitted 3372; viewer decoded 3303, received
  3304, and submitted 3329, including 3229 post-boundary submissions.
- The runner retained 44 post-resume samples. Host/viewer video continuity
  maxima were one/zero stalled samples.
- Bidirectional synthetic primary voice advanced through the call: host received
  2941 and sent 2908 packets; viewer received 2902 and sent 2935 packets.
- The owned fixture started, remained alive for the measurement, and stopped on
  cleanup.

### Lock/unlock with capture error during evaluation

Artifact: ignored
`out/macos-session-lifecycle-recovery/controlled-lock-capture-fault-60s-rerun.jsonl`

SHA-256:
`3bb422e2df7b7534282b35e7abbb07392edeb8253f3900a4f89aec92c398b015`

- Both peers acknowledged the final unlock event before the host's controlled
  ScreenCaptureKit delegate fault was injected.
- The host made one `capture-restarted` post-resume decision; the viewer made a
  healthy decision. Capture attempts/successes/generation advanced exactly once
  and did not race the 750 ms evaluation boundary.
- Host encoded 3347 frames and submitted 3397; viewer decoded 3338, received
  3339, and submitted 3390, including 3279 post-recovery submissions.
- The runner retained 45 post-resume samples. Host/viewer video continuity
  maxima were one/one stalled samples.
- Bidirectional synthetic primary voice advanced: host received 2943 and sent
  2910 packets; viewer received 2903 and sent 2936 packets.

The earlier ignored artifact named
`controlled-lock-capture-fault-60s.jsonl` is explicitly invalid and excluded.
It exposed the now-fixed summary-finalization bug and is not acceptance
evidence. Other pre-review lifecycle artifacts are likewise not cited.

Accepted executable identities:

- `shareme_rtc_demo` SHA-256
  `dc6387567c883a27a7112241ccc655d47065c9dbb61b0511cad8d7d0fcafbf6d`
- `shareme_screen_motion_fixture` SHA-256
  `5cc8a9a4b6bda5f8590fd69b197852da99a8d14738a16c95a9da0a0fce06ec52`

## Automated verification

- Fresh `call-dev` configure/build completed; configured CTest passed `55/55`.
- `signaled_peer` lifecycle repetition passed `20/20`.
- Screen runner contracts passed `26/26` and lifecycle runner contracts passed
  `6/6` under both Homebrew Python 3 and `/usr/bin/python3`.
- `go test -race ./...` and `go vet ./...` passed for signaling.
- Sol-Terra workflow tests passed `8/8`; ShareMe skill validation passed.
- The portable-core forbidden-header scan and `git diff --check` passed.
- Redaction scans of the two accepted artifacts found no local user path, room,
  token, SDP, ICE, or WebSocket address.
- Repository-external WebRTC checkout and depot-tools worktrees remained clean.
- Independent read-only review found no remaining Critical or Important issue
  after the evaluation/recovery race and runner-causality findings were fixed.

## Evidence boundaries

- **Verified:** controlled nested lifecycle policy; one healthy post-resume
  decision; controlled capture-error classification after the final event;
  bounded same-source recovery; H.264 VideoToolbox; matching geometry; video and
  bidirectional synthetic-voice counter progress; cleanup and Apple build.
- **Partial:** native AppKit/Foundation observer symbols, ownership, compilation,
  and translation seams are verified, but this session did not physically emit
  the four operating-system notifications.
- **Environment-dependent / not run:** real Mac sleep/wake, real lock/unlock,
  physical display scanout, audible microphone/speaker continuity, acoustic A/V
  synchronization, thermals, permission revocation, display removal, lid-close
  behavior, and physical 4K.
- **Not verified by this stage:** Windows native execution. Apple behavior is
  compile-time isolated and portable tests pass, but that is not a Windows run.
- **Unimplemented:** automatic signaling reconnect/rejoin and any change to
  quality, adaptation, cursor composition, or capture/codec parameters.

## Handoff

The next macOS evidence task is an attended physical campaign using the
runner's `physical-wait` mode: first lock/unlock, then sleep/wake, while retaining
the same H.264 VideoToolbox, geometry, liveness, video, voice, and post-resume
gates. Product work on automatic reconnect/rejoin should begin only if physical
evidence demonstrates that the current retryable connection-loss boundary is
insufficient. No file-sharing, Movie Stage 2B, hard-resync, or quality reduction
is authorized by this result.
