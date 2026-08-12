# macOS Screen Capture Restart Recovery Design

## Outcome

Prove that an active high-quality macOS screen-sharing call survives a real
ScreenCaptureKit stream teardown and recreation without rebuilding the WebRTC
session or interrupting bidirectional voice. Video at both peers must continue
within five observation samples and all existing quality gates remain frozen.

## Evidence-driven approach

The initial design paused the runner-owned motion fixture with `SIGSTOP` and
resumed it with `SIGCONT`. Two 60-second diagnostics showed that ScreenCaptureKit
can continue delivering frames while visible content is static; neither native
nor offscreen peers produced a reliable counter plateau. A static-content stall
is therefore not a truthful prerequisite for capture recovery.

The accepted design keeps that bounded content pause as an additional stress
condition but also performs an actual native capture restart. The host retains
the `ScreenVideoSource` used by its unchanged WebRTC video track. A test-only,
bounded environment probe calls `stop()` and `start()` on that same source once
during the live call. On macOS this destroys and recreates `SCStream`; the peer
connection, H.264 encoder selection, audio tracks, and UI session remain alive.

## Components and interfaces

`RtcDemoController`:

- retains the host `ScreenVideoSource` while the peer factory receives the same
  ref-counted instance;
- accepts `SHAREME_SCREEN_CAPTURE_RESTART_TRIGGER_FILE` only in macOS builds;
- polls for a one-use private trigger created by the runner at the fixture
  interruption boundary, then performs exactly one stop/start cycle;
- emits monotonic counters for restart attempts, successes, and generation;
- reports a sanitized failure status if restart fails.

`run_screen_stream_smoke.py`:

- accepts paired fixture interruption timing for macOS only;
- pauses/resumes only its owned fixture and always resumes it before teardown;
- activates the native restart probe at the same initial boundary;
- records role-aligned host/viewer counter samples for the interruption and
  resume phases;
- requires the host restart counters to transition exactly from `0/0/0` to
  `1/1/1` inside the probe window;
- requires every host and viewer video-stage counter to advance within five
  samples after resume, voice counters to advance at every probe sample, and at
  least ten post-resume samples;
- records sanitized JSONL only, excluding paths, PIDs, rooms, SDP, ICE, and
  child output.

## Lifecycle and failure behavior

1. Validate macOS, fixture ownership, warmup, 3–5 second fixture pause, and a
   ten-second post-recovery observation window before artifact creation.
2. Start the fixture, signaling, host, and viewer through existing guarded
   lifecycle paths.
3. Start the host trigger poller only after the peer has started.
4. At the runner boundary, record role-specific counter indices, pause the
   fixture, and publish the private one-use trigger. The host tears down and
   restarts its native capture.
5. Require exact host acknowledgement before resuming the fixture and recording
   the second role-specific boundary; timeout is a hard failure with cleanup.
6. Preserve the existing full-call continuity, H.264 VideoToolbox, geometry,
   bitrate, queue, conversion, presentation-recovery, and synthetic voice gates.
7. Apply the restart generation and post-resume recovery gates.
8. Resume a stopped fixture before normal group termination on every exit path.

Any missing generation transition, duplicate attempt, restart failure, voice
stall, late or partial video recovery, early process exit, missing counter, or
insufficient post-window is a hard failure. No-data evidence is never accepted.

## Verification

- RED/GREEN Python tests cover configuration, exact signals, cleanup order,
  role-aligned phases, counter parsing, exact restart generation, five-sample
  video recovery, continuous voice, and rejection boundaries.
- The C++ source test proves one `ScreenVideoSource` instance can stop, start,
  and deliver frames again without replacing the source object.
- The complete affected Python suites pass under system and Homebrew Python.
- Fresh `call-dev` build, 51-test CTest, and 20 repeated `signaled_peer` runs
  pass against the preserved external libwebrtc archive.
- A native standard-profile call runs for 60 seconds, restarts ScreenCaptureKit
  after 15 seconds while the fixture pauses for three seconds, and passes every
  frozen gate with exactly one successful capture generation.
- Go race/vet, workflow, skill, portable-core, redaction, Git, and cache hygiene
  gates pass.

## Boundaries

This proves a controlled native capture stop/start on macOS. It does not yet
implement automatic retry after an unsolicited `SCStreamDelegate` error and
does not prove real minimize/restore, occlusion semantics, lock, display
sleep/wake, permission revocation, physical scanout, subjective image quality,
audible voice, thermal behavior, Windows, or 4K.

No resolution, cadence, codec, bitrate, queue bound, drop policy, or quality
threshold is reduced. Generated output stays ignored and the external
libwebrtc cache remains read-only.
