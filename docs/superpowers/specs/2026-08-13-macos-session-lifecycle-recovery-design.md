# macOS Session Lifecycle Recovery Readiness Design

## Outcome

ShareMe observes macOS sleep/wake and screen lock/unlock as explicit lifecycle
episodes while a screen-sharing call is active. It keeps the existing call and
voice paths intact during suspension, prevents capture recovery timers from
racing a suspended session, and evaluates the post-resume state before taking
the smallest safe action.

If signaling and the peer remain healthy and ScreenCaptureKit reports a
recoverable error, ShareMe reuses the existing bounded same-source recovery. If
the signaling transport or peer is no longer healthy, ShareMe enters the
existing retryable call-result flow with a sanitized lifecycle category; it
does not claim that capture-only recovery repaired the whole call.

This stage builds and validates the product lifecycle boundary and a safe
physical-acceptance harness. It does not programmatically sleep or lock the Mac
from an unattended agent session. Physical sleep/wake and lock/unlock become
Verified only after their real system notifications and post-resume media are
recorded in an authorized run.

## Selected architecture

A Qt-free `SessionLifecyclePolicy` owns event ordering and one lifecycle
generation. Its inputs are `will_sleep`, `did_wake`, `screen_locked`, and
`screen_unlocked`; its outputs are inactive, suspended, evaluating, recovered,
or failed. Duplicate suspend/resume notifications are idempotent. A resume
without an active suspension and a stale generation cannot start evaluation.

A macOS adapter observes `NSWorkspaceWillSleepNotification`,
`NSWorkspaceDidWakeNotification`, and the distributed session lock/unlock
notifications. It emits typed callbacks only; it does not touch ScreenCaptureKit,
WebRTC, Qt UI, or recovery policy. A default no-op adapter keeps Windows and
other platforms unchanged.

`RtcDemoController` owns orchestration:

1. On the first suspend notification, stop the capture-error and recovery
   timers, cancel the active retry episode without stopping the peer, and show
   `session-suspended:<sleep|locked>`.
2. On the matching resume notification, show `session-resuming`, wait one
   bounded 750 ms settling interval, then inspect signaling state, peer media
   availability, and the video-source error.
3. If signaling/peer are unavailable, publish
   `call-error: session-resume-connection-lost` and preserve the retryable GUI.
4. If the screen source has a recoverable error, begin the existing automatic
   recovery episode on the retained source.
5. If the call and source remain healthy, return to `connected` and restart
   error monitoring without restarting capture.

The controller never rebuilds signaling, PeerConnection, tracks, codecs, or
audio routes in this stage. Automatic room rejoin is deliberately excluded:
the current signaling session has no reconnect/rejoin contract, and hiding that
gap behind capture recovery would be unsafe.

## Lifecycle and concurrency contract

- One active lifecycle generation exists at a time.
- Sleep and lock may nest; evaluation begins only when both sleep and lock are
  clear. For example, wake while still locked remains suspended until unlock.
- Repeated notifications do not increment generation or schedule duplicate
  evaluation.
- Shutdown stops the settle timer, unregisters native observers, resets policy,
  and prevents queued callbacks from changing the result page.
- Capture recovery active before suspension is cancelled and may restart only
  from the post-resume evaluation.
- Native notifications are delivered onto the controller's Qt thread through a
  queued callback; no Objective-C notification callback blocks on Qt, WebRTC,
  or ScreenCaptureKit.

## User-visible behavior

The call page remains visible during suspension and resume evaluation. The top
bar and video stage display “系统暂停，等待恢复” and “正在恢复通话”. Microphone,
speaker, details, and leave controls remain present, although macOS may stop
physical device activity during system sleep. A connection-loss result uses the
existing retry/home flow and a friendly message; raw notification names and
native errors are never shown.

## Diagnostic and physical acceptance harness

An opt-in macOS diagnostic input may inject the same typed lifecycle events
without posting operating-system notifications. It verifies policy,
controller, UI, timers, cleanup, and healthy/capture-error/connection-lost
branches, but is labeled controlled evidence.

The physical runner does not invoke `pmset`, AppleScript, CGSession, or any
password/credential operation. It starts server, host, viewer, and the moving
fixture, writes a sanitized instruction marker, and waits for real native
sleep/wake or lock/unlock notifications. An external watchdog records only
bounded booleans, monotonic lifecycle generations, process liveness, and media
counters. Timeout resumes cleanup and fails the run.

Physical acceptance requires two separate runs:

- sleep/wake: one real sleep and wake, notification ordering, processes alive,
  bounded post-wake evaluation, VideoToolbox H.264, matching geometry, video
  recovery within five samples, bidirectional synthetic voice after wake, and
  at least ten post-recovery samples;
- lock/unlock: one real lock and unlock with the same media gates.

No artifact stores username, path, notification payload, room, PID, server
address, token, SDP, ICE, or raw process output.

## Frozen boundaries and evidence labels

Resolution, cadence, bitrate, codec selection, queue depth, cursor, adaptation,
presentation recovery, audio routing, and retry delays are frozen. External
WebRTC caches remain read-only. Generated builds and lifecycle artifacts remain
ignored.

- **Verified:** policy ordering, macOS adapter registration/translation,
  controlled controller branches, UI status, cleanup, and non-regression on the
  platform where their tests run.
- **Partial:** a controlled lifecycle notification run without a physical OS
  transition.
- **Environment-dependent:** real sleep/wake and lock/unlock until a person
  performs the event; display removal without external hardware; audible voice,
  scanout, thermals, Windows native, and physical 4K.
- **Unimplemented:** automatic signaling reconnect/rejoin, permission recovery,
  system audio, HDR, remote input, TURN, file sharing, and 4K60.
