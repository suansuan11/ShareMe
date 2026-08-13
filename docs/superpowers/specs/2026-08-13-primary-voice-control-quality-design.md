# Primary Voice Control and Quality Diagnostics Design

## Outcome

ShareMe makes the bidirectional call-voice path understandable and controllable
without weakening screen quality. During a call, each participant can mute the
local microphone, mute remote voice, set native speaker volume, see whether the
microphone is producing an audio signal, and see a conservative voice-quality
status derived from WebRTC statistics.

This stage does not capture macOS or Windows system/application audio. Shared
system audio is a distinct, host-to-viewer media path and will receive its own
design after primary voice is accepted. Movie audio also remains independent.

## Selected architecture

The locked libwebrtc audio device remains the only owner of microphone capture,
native voice playout, acoustic echo cancellation, noise suppression, and
automatic gain control. `SignaledPeer` retains a reference to the primary-voice
audio device so it can apply a normalized speaker volume while the call is
active. A failed device operation returns failure and leaves the last accepted
controller/UI value unchanged.

The existing one-second WebRTC statistics poll adds only primary-voice fields:
local microphone level, inbound packets lost, jitter, concealed samples, and
total received samples. A small Qt-free `VoiceQualityPolicy` converts one
snapshot into `checking`, `good`, `unstable`, `poor`, or `muted`. It never
classifies missing statistics as good, and it does not mix movie-audio or
screen-video counters into voice health.

`RtcDemoController` exposes the normalized speaker volume, microphone activity,
quality category, and a short sanitized message to QML. The existing microphone
and speaker buttons keep their behavior. The call details drawer gains an
always-visible voice section with a microphone activity meter, speaker volume
slider, processing summary, and quality status. Advanced movie-audio diagnostics
remain separate below it.

## Control contract

- Microphone mute enables/disables only the local primary-voice audio track.
- Speaker mute enables/disables only native remote primary-voice playout.
- Speaker volume accepts integer values from 0 through 100 and maps them to the
  native audio device's reported minimum and maximum. Volume 0 is not an alias
  for mute; mute remains a separate reversible control.
- If native playout is disabled for a deterministic smoke run, volume control is
  unavailable and must not claim success.
- The last accepted volume survives mute/unmute within one call. Device
  hot-switch persistence and cross-call preference persistence are excluded.
- Existing microphone processing remains explicit: echo cancellation, noise
  suppression, and automatic gain control are enabled only for microphone
  sources. Synthetic voice and movie audio remain unprocessed.

## Quality and privacy contract

The UI updates at the existing one-second diagnostic cadence. Local microphone
activity is a clamped 0-100 visualization derived from WebRTC's normalized
audio level; it is zero and labeled muted while the local track is disabled.

Voice quality is conservative:

- `checking`: required inbound statistics are absent or no receive interval is
  available;
- `muted`: remote primary-voice playout is disabled;
- `good`: interval packet loss is at most 2%, jitter is at most 30 ms, and
  concealment is at most 2%;
- `unstable`: loss is at most 5%, jitter is at most 60 ms, and concealment is at
  most 5%, but the `good` boundary is exceeded;
- `poor`: any available interval exceeds the `unstable` boundary or a cumulative
  counter regresses.

The policy compares consecutive cumulative snapshots so long calls are not
masked by early good samples. It stores no samples, microphone audio, device
names, room identifiers, or network addresses. The UI language describes
transport symptoms and never claims subjective acoustic quality.

## MotionFixture policy

MotionFixture is a test-only moving-window process. It remains available only
to unattended gates whose acceptance requires proof of changing screen pixels,
such as screen cadence, capture restart, or physical lifecycle recovery.

Primary-voice development, QML checks, local call-control tests, synthetic
voice smoke runs, and human audible acceptance must not start MotionFixture.
Existing screen-motion gates keep their explicit fixture requirement; this
stage does not weaken or silently bypass those gates.

## Verification

Automated verification includes Qt-free policy tests, audio-device volume
mapping/failure tests, `SignaledPeer` control and statistics tests, controller
state tests, QML contract tests, and one bounded macOS two-peer synthetic voice
run with native playout disabled. The runner must record that no MotionFixture
was requested or started.

A separate human two-device acceptance remains required before audible voice is
Verified. Both participants must confirm intelligible speech, mute/unmute,
speaker volume changes, no persistent echo, and voice continuity while the host
shares a real screen. Physical speaker audibility, subjective echo/noise
quality, device hot-switching, system audio, Windows native behavior, and
acoustic A/V synchronization remain environment-dependent or unimplemented
until their named evidence exists.

## Frozen boundaries

Screen resolution, cadence, bitrate, codec selection, VideoToolbox/Media
Foundation behavior, capture recovery, lifecycle recovery, video queues, movie
audio, signaling, and room behavior remain unchanged. No new dependency is
introduced, and the repository-external libwebrtc cache remains read-only.
