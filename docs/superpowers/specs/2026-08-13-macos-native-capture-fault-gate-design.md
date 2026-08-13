# macOS Native Capture Delegate Fault Gate Design

## Outcome

Add a macOS-only, opt-in diagnostic path that injects a sanitized stop error
through the active `SCStreamDelegate::didStopWithError` callback. The existing
controller must observe the resulting `screen-capture-stopped-*` category,
perform exactly one successful bounded recovery, and keep the existing call,
video track, VideoToolbox encoder, and bidirectional voice alive.

After recovery, the diagnostic path injects a second stop callback through the
retired delegate. The retired callback must be rejected by its captured native
generation: it cannot set the replacement stream's error, start a second
recovery episode, change the restart counters, or interrupt new video frames.

This is a controlled software fault gate. It does not claim to reproduce a
physical sleep/wake, lock, permission-revocation, or display-removal event.

## Selected architecture

The existing controller-level trigger is replaced for this acceptance mode by
a narrow diagnostic command sent to the retained `ScreenVideoSource`. The
command is available through default-false virtual methods at the screen
adapter boundary, so Windows and normal product execution do not gain any
behavior unless the macOS smoke runner explicitly configures the trigger.

`ScreenCaptureKitStream` remains the owner of the active native stream and
delegate. For an enabled diagnostic episode it retains the delegate that
reported the current synthetic stop until the replacement stream has started.
The synthetic `NSError` is sanitized and passed by directly invoking that
delegate's real `stream:didStopWithError:` method. Therefore the tested path is
the same Objective-C delegate block and `handle_stream_error` implementation
used by an unsolicited ScreenCaptureKit error; the controller does not call
`beginScreenCaptureRecovery` directly.

The second diagnostic command invokes `didStopWithError:` on the retained old
delegate. Its block carries the retired generation, so the existing two-phase
generation check must reject it. A successful stale-event check releases the
retired delegate. Shutdown and failed startup also release all diagnostic
state.

Alternatives rejected:

- Testing `MacScreenCaptureEventGate` alone does not prove Objective-C delegate
  routing.
- Direct controller error injection proves the retry policy but bypasses the
  native error boundary.
- Rebuilding the PeerConnection or video track would violate the established
  recovery and voice-continuity contract.

## Diagnostic command and lifecycle contract

The screen adapter exposes two opt-in commands:

1. `inject_current_stream_stop_for_diagnostics()` invokes the active native
   delegate exactly once, retains that delegate as the retired diagnostic
   source, and returns false when no active stream exists or the episode is
   already armed.
2. `inject_retired_stream_stop_for_diagnostics()` invokes the retained old
   delegate exactly once after a replacement stream exists, then releases it.
   It returns false when no retired delegate is available.

These commands are not general recovery APIs. They do not call `stop()`,
`start()`, the retry policy, signaling, or WebRTC. The normal error monitor is
solely responsible for discovering the current delegate error and entering the
automatic recovery policy.

The controller polls two runner-owned trigger files only on macOS. The current
trigger remains active until the native command acknowledges injection. After
the exact `0/0/0 -> 1/1/1` recovery transition, the stale trigger invokes the
retired delegate. Missing acknowledgement is a hard smoke failure, and all
timers and retained diagnostic objects are cleared during `stopPeer()`.

## Frozen media and platform boundaries

This stage must not change screen dimensions, frame rate, bitrate, codec
selection, queue depth, cursor policy, adaptation, presentation recovery,
audio routing, signaling, or retry delays. The existing H.264 VideoToolbox and
same-source recovery contracts remain frozen.

All diagnostic trigger parsing, controller calls, and native implementation are
compiled only for Apple platforms. Default execution has no trigger paths and
Windows behavior remains unchanged. Generated JSONL, logs, build trees, trigger
files, local settings, and external WebRTC caches are never committed.

## Error handling and observability

The injected current error uses a fixed synthetic numeric `NSError` code and is
exposed only as a sanitized `screen-capture-stopped-<number>` category. Raw
localized descriptions, paths, rooms, tokens, SDP, ICE, PIDs, and child output
must not enter QML or JSONL.

The smoke artifact records booleans and bounded counters for:

- native current-delegate injection acknowledged;
- automatic recovery status observed before restart success;
- exact one-attempt/one-success/one-generation transition;
- retired-delegate injection acknowledged;
- restart counters unchanged after the stale event;
- host/viewer video and bidirectional voice progress after both boundaries.

No pointer, native object identity, trigger path, or raw NSError is recorded.

## Verification and acceptance

Automated tests must first fail, then pass, for:

- diagnostic command forwarding through `ScreenVideoSource` and the macOS
  backend, with false/default behavior on unsupported backends;
- current delegate injection setting the expected sanitized stream error;
- recovery replacing the stream and a late retired-delegate error leaving the
  replacement error empty;
- one-shot command acknowledgement, cleanup, and unavailable-state rejection;
- runner ordering and classification for current injection, recovery, stale
  injection, unchanged counters, and post-stale media samples;
- compile-time Apple scoping and default/Windows isolation.

Native acceptance is one 60-second standard-profile macOS Cocoa call with the
owned moving fixture. At 15 seconds the fixture pauses and the runner triggers
the current delegate fault. The fixture resumes only after exact first-attempt
recovery acknowledgement. After at least two subsequent counter samples the
runner injects the retired delegate error. Acceptance requires H.264
VideoToolbox, matching geometry, exact `0/0/0 -> 1/1/1`, no second recovery,
host/viewer video recovery within five samples, later video samples after the
stale injection, continuous bidirectional synthetic voice, bounded viewer
presentation recovery, and at least ten final post-stale samples.

Full affected CTest, repeated `signaled_peer`, Python runner suites, Go
race/vet, workflow tests, skill validation, portable-core scan, redaction,
cache-preservation checks, and `git diff --check` remain mandatory.

Evidence labels are explicit:

- **Verified:** controlled delegate invocation, error propagation, bounded
  automatic recovery, retired-delegate rejection, and media continuity on the
  named macOS run.
- **Partial:** correspondence between the controlled synthetic NSError and all
  possible ScreenCaptureKit operating-system errors.
- **Environment-dependent:** physical sleep/wake, lock, permission revocation,
  display removal, audible voice, scanout, thermal behavior, Windows native
  rerun, and physical 4K displays.
- **Unimplemented:** system audio, HDR, remote input, TURN, file sharing, and
  4K60 optimization.
