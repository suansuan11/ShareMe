# macOS Session Lifecycle Recovery Verification

## Result

`verified-physical-lock-and-clamshell-recovery`

ShareMe now observes macOS sleep/wake and session lock/unlock without rebuilding
a healthy call. Nested lifecycle causes are folded into one generation, capture
recovery is suspended while the session is inactive or evaluating, and exactly
one post-resume decision is made after a 750 ms settle window. A healthy call
returns to `connected`; a recoverable ScreenCaptureKit error reuses the existing
bounded same-source recovery; an unavailable peer, signaling path, or media
snapshot enters the existing retryable result page.

The evidence is split into controlled application-level events and attended
physical operating-system behavior. The runner never calls `pmset`, AppleScript,
CGSession, a password API, or a display operation, and never posts native macOS
notifications. The final attended runs physically locked/unlocked the session
and entered clamshell sleep; macOS power logs independently recorded the final
Sleep/Wake interval.

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
- Physical sleep samples are excluded from the ordinary continuity calculation
  only when each role has the complete generation-1 lifecycle sequence and one
  same-generation recovery marker. Pre-suspension and post-recovery samples keep
  the original five-stall limit, and lifecycle validation still requires video
  and every primary-voice counter to recover within five samples.
- In physical mode, the unique host recovery marker authorizes exactly zero or
  one matching capture restart. Controlled mode retains its fixed injected-fault
  expectation, and the viewer must remain healthy in both modes.
- The restart gate also requires `0/0/0` through the suspension boundary, one
  synchronous `1/1/1` transition no later than the same-role recovery marker,
  and stable `1/1/1` afterward. A pre-sleep restart, split counter transition,
  regression, or nonzero healthy-call counter fails closed.

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

## Attended physical evidence

### Physical lock/unlock

Artifact: ignored
`out/macos-session-lifecycle-recovery/physical-lock-unlock-90s.jsonl`

SHA-256:
`705bdccdcec943c037e8a89efb155e6a625ffd80efa3bcabf0fd11d11ec5b7c6`

- Both processes received physical `screen-locked -> screen-unlocked` in one
  generation and returned healthy without restarting ScreenCaptureKit.
- H.264 VideoToolbox remained active at matching `1470x956` geometry.
- Host encoded 4934 and submitted 5012 frames; viewer decoded 4923, received
  4924, and submitted 4956 frames.
- The run retained 65 post-unlock samples. Host/viewer maximum video stalls were
  zero/one sample outside lifecycle boundaries.
- Host received/sent 4465/4414 primary-voice packets; viewer received/sent
  4406/4457 packets.

### Physical clamshell sleep/wake

Artifact: ignored
`out/macos-session-lifecycle-recovery/physical-clamshell-sleep-wake-final-180s.jsonl`

SHA-256:
`3867e41e69c55b51d733929989b3851fdd40a0be50086be037c835a45c68031a`

- macOS power evidence recorded `Clamshell Sleep` at 16:55:30 +0800 and a
  `UserActivity` full wake at 16:56:20 +0800: 50 seconds of physical sleep.
- Both processes observed `screen-locked -> will-sleep -> did-wake ->
  screen-unlocked` in generation 1.
- The host made one `capture-restarted` decision; attempts, successes, and
  ScreenCaptureKit generation advanced exactly `0/0/0 -> 1/1/1`. The viewer
  returned healthy and the existing PeerConnection, signaling, and voice tracks
  remained in place.
- The final artifact was manually checked at this boundary: all three host
  restart counters were `0/0/0` through sample 81, changed together to `1/1/1`
  at sample 82, and stayed there. The post-review runner now enforces this
  causal transition for future artifacts rather than checking only final values.
- H.264 VideoToolbox remained active with matching `1470x956` geometry. Host
  encoded 6815 and submitted 6953 frames; viewer decoded 6801, received 6803,
  and submitted 6813 frames, including 6703 post-recovery submissions.
- The causally proven suspension range contained 67 samples per role. Outside
  that range, host/viewer maximum stalls were zero/one sample. The stricter
  lifecycle recovery gate passed within five samples and retained 96
  post-resume samples.
- Host received/sent 9044/8939 primary-voice packets; viewer received/sent
  8929/9033. All four packet and byte counters passed the five-sample recovery
  window.

### Failed diagnostic runs

These artifacts are evidence of discovered gate defects, not accepted runs:

- `physical-sleep-wake-150s.jsonl`, SHA-256
  `76b4dcbc6d056362ef4e3370f7055e7d02684d7cb60db661df3dc12b7177a581`:
  failed and is not physical sleep evidence. It emitted two lock/unlock
  generations, no `will-sleep`/`did-wake`, and the matching macOS power window
  contained no Sleep/Wake record.
- `physical-clamshell-sleep-wake-180s.jsonl`, SHA-256
  `1ac453dacad15944dbd59d1111e6d7f9c3f71d2d90754c2d70ac3e5327901cfc`:
  valid clamshell reproduction that failed because the base runner counted the
  deliberate suspension as an ordinary encoded-frame stall.
- `physical-clamshell-sleep-wake-rerun-180s.jsonl`, SHA-256
  `22d8985b3cbc00d8797271212461f8f9d02ac6cb5c128471ffe5ac06823a8dbf`:
  crossed the corrected continuity gate but exposed a second false rejection:
  physical mode still expected zero restarts despite the unique
  `capture-restarted` marker and exact `1/1/1` counters.

No failed artifact was reclassified. Both runner defects were reproduced by a
failing unit test before the minimal implementation change, and the final
physical run was collected from a new artifact.

## Automated verification

- Fresh `call-dev` configure/build completed; configured CTest passed `55/55`.
- `signaled_peer` lifecycle repetition passed `20/20`.
- Screen runner contracts passed `27/27` and lifecycle runner contracts passed
  `8/8` under both Homebrew Python 3 and `/usr/bin/python3`.
- `go test -race ./...` and `go vet ./...` passed for signaling.
- Sol-Terra workflow tests passed `8/8`; ShareMe skill validation passed.
- The portable-core forbidden-header scan and `git diff --check` passed.
- Redaction scans of the accepted physical artifact found no local user path,
  room, token, SDP, ICE, or WebSocket address.
- Repository-external WebRTC checkout and depot-tools worktrees remained clean.
- Independent read-only review found no remaining Critical or Important issue
  after the evaluation/recovery race and runner-causality findings were fixed.

## Evidence boundaries

- **Verified:** controlled nested policy and recovery; attended physical
  lock/unlock; attended clamshell sleep/wake; native notification translation;
  one bounded post-wake ScreenCaptureKit restart; retained PeerConnection and
  primary-voice tracks; H.264 VideoToolbox; matching geometry; video and
  bidirectional synthetic-voice counter recovery; cleanup and Apple build.
- **Partial:** the final call proves counter-level synthetic primary-voice
  continuity, not human-audible microphone/speaker continuity or acoustic A/V
  synchronization.
- **Environment-dependent / not run:** Apple-menu sleep without clamshell,
  repeated/long-duration sleep cycles, physical display scanout, audible
  microphone/speaker continuity, acoustic A/V synchronization, thermals,
  permission revocation, display removal, and physical 4K.
- **Not verified by this stage:** Windows native execution. Apple behavior is
  compile-time isolated and portable tests pass, but that is not a Windows run.
- **Unimplemented:** automatic signaling reconnect/rejoin and any change to
  quality, adaptation, cursor composition, or capture/codec parameters.

## Handoff

The tested physical lock and clamshell paths kept the call alive, so this result
does not justify automatic signaling reconnect/rejoin. The next Mac acceptance
task is a two-person audible voice and visual screen-sharing pass, followed by
cursor composition and display selection. A later repeat/long-sleep campaign may
extend evidence without changing the frozen quality gates. No file-sharing,
Movie Stage 2B, hard-resync, or quality reduction is authorized by this result.
