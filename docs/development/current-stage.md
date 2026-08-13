# ShareMe Current Development Stage

This is the canonical dynamic handoff. Verify it against current Git, source,
tests, and verification evidence before relying on it.

## Delivered baseline

- `main` includes the receiver playback-state channel through merge `87138a9`.
- The simplified repository workflow is merged on `main` through `834c917`.
  This corrects the previous stale claim that the workflow branch was awaiting
  authorization. Root `AGENTS.md`, the ShareMe skill, Sol/Terra role files, and
  the two-child configuration are active for new trusted ShareMe tasks.
- The active workflow migration is delivered on
  `codex/shareme-sol-terra-migration`: the Codex client or user configuration
  selects the root model for new tasks, Terra is the medium-reasoning child
  route, and one writer remains mandatory for every implementation scope.
- The deterministic ShareMe workflow stage is delivered on
  `codex/shareme-workflow-simplify` and integrated by that merge. The remote
  branch remains historical backup evidence, not pending work.
- Windows movie/microphone regression and Desktop Duplication evidence remain
  recorded in their linked verification documents; this macOS stage does not
  replace or extend those Windows claims.

### Complete Application GUI

The complete GUI stage is delivered on `codex/complete-gui` and is ready for
integration. Launching `shareme_rtc_demo` without RTC options now opens the
ShareMe application: Calm Dark home, create/join preflight, one-recent-room
privacy boundary, settings/help, focus-stage call view, local or remote video,
live microphone/speaker controls, details diagnostics, leave, and recovery.
Explicit command-line calls remain strict and compatible with existing smoke
and diagnostic runners.

On macOS 26.6.1 arm64, the GUI lifecycle gate passed all six states and real
control actions. `call-dev` passed 47/47 and `movie-call-dev` passed 72/72;
`signaled_peer` repeated 20/20. Separate native standard 10-second, quality
30-second, and cinema 30-second calls all kept H.264 hardware encoding active,
matched 1470x956 host/viewer geometry, moved video and bidirectional voice
counters, and submitted frames after the bounded presentation recovery. See
[Complete GUI Verification](../verification/complete-gui.md).

Windows GUI hardware execution, human visual/acoustic acceptance, physical
temperature, physical 1440p/4K displays, device hot-switching, and production
packaging remain environment-dependent, unverified, or unimplemented. No
resolution, cadence, bitrate, codec-quality, or media gate was lowered.

### Hardware Screen Streaming Foundation

`main` includes the accepted macOS ScreenCaptureKit and VideoToolbox screen
path through implementation/fix tip `77722c3`. The source branch
`codex/hardware-screen-streaming` was committed and published before local
integration. On macOS arm64, the `build/movie-call-dev` build and 64-test CTest
suite pass; the Qt/WebRTC-only `call-dev` configuration also builds without
Movie/FFmpeg and passes 39/39. The documented
10-second and 30-second `standard`, 30-second `quality`, and 30-second
`cinema` smoke gates, plus a 120-second `cinema` stability run, negotiated
H.264 with active VideoToolbox encoding, bounded queues, nonzero bitrate, host
encode counters, and viewer decode counters. The final artifacts are under
`out/hardware-screen-streaming` and are evidence only.

The earlier zero-output native run was traced to the locked WebRTC H.264
factory advertising Level 3.1 (`640c1f`/`42e01f`) for screen profiles whose
configured bounds require higher levels. VideoToolbox returned
`kVTParameterErr` (`-12902`). The local factory now preserves the H.264
profile and packetization parameters while advertising Level 4.2 for
`standard` and Level 5.1 for `quality`/`cinema`.

The current macOS display produced `1470x956` frames, so exact target-resolution
behavior, visual frame integrity, foreground/background recovery, and live
voice continuity remain partial or environment-dependent. Windows native
screen evidence remains separate and unverified.

### Screen Streaming Quality and Voice Acceptance

`main` includes the accepted screen/voice stage through fast-forward
integration tip `9e2a23f`; its reviewed code tip is `211b783`. Interactive RTC
demo calls now use
the real microphone and native primary-voice playout by default; deterministic
smoke calls explicitly select synthetic voice and disable speaker playout.
One background stats poller reports both video and primary-voice RTP totals,
excluding independent movie audio.

On macOS 26.6.1, Apple M4 arm64, `call-dev` passed 39/39 and
`movie-call-dev` passed 64/64. Standard 10/30-second, quality 30-second,
cinema 30-second, and cinema 120-second native runs all kept H.264
VideoToolbox active, reported identical 1470x956 geometry at both peers,
maintained bidirectional voice counter progress, and recovered exactly once
from a screen-only bounded presentation close/reopen without replaying a stale
frame. The two-minute run delivered 3471 host encoded and 3498 viewer decoded
frames, roughly six thousand voice packets per direction, and 3397
post-recovery viewer submissions. An independent final review found no Critical
or Important issue; its one recovery-counter locking Minor was fixed and the
concurrent snapshot test passed 20 consecutive runs. See
[Screen Streaming Quality and Voice Acceptance](../verification/screen-streaming-quality-voice.md).

A separate native microphone probe verified nonzero microphone levels and
bidirectional audio RTP. Actual speaker audibility, subjective echo control,
human visual integrity, physical foreground/background behavior, temperature,
and physical 1080p/1440p/4K display coverage remain partial or
environment-dependent.

Windows now uses the existing Desktop Duplication screen source with a guarded
native Media Foundation H.264 encoder and decoder. Hardware selection requires
a successful `MFT_ENUM_FLAG_HARDWARE` probe plus real encoder and decoder
initialization; explicit software diagnosis remains standard-only VP8. On
Windows 11 AMD64 with MSVC 19.51, `call-dev` passed 55/55 and `movie-call-dev`
passed 80/80. Three 180-second 1920x1080 software baselines and three matching
hardware runs from the same executable passed the frozen gate: median host CPU
mean fell from 6.5545% to 4.1118% (37.2675%), CPU P95 improved, RSS P95 changed
by -14.9605%, and hardware submitted cadence was 99.9868%. A 120-second
2560x1440 quality run kept Media Foundation H.264 active at 99.9611% cadence
with video, bidirectional synthetic primary voice, and bounded recovery.

The stage outcome is `partial-windows-hardware-evidence`: Windows automated GUI
and hardware screen gates are verified, while two-device human visual/acoustic
acceptance, physical 4K, thermal observation, cursor composition, display
selection, and this branch's native macOS regression remain not-run or
environment-dependent. See
[Windows GUI and Hardware Screen Parity Verification](../verification/windows-gui-hardware-parity.md).

### macOS Evidence Hardening

The portable evidence hardening stage is delivered on
`codex/macos-evidence-hardening`. New screen-smoke runs carry an independent
random `run_id`; formal performance comparison rejects reused artifact paths or
run identities, malformed executable SHA-256 values, and any missing,
duplicated, unordered, or invalid one-second process sample in the 30-through-
150-second measurement window. The existing Windows three-plus-three result is
preserved as historical evidence, but a replacement formal result must be
collected on Windows with six independent artifacts under this hardened
contract.

On macOS 26.6.1 arm64, fresh `call-dev` and `movie-call-dev` builds passed
51/51 and 76/76 CTest respectively, and `signaled_peer` repeated 20/20. A
standard 10-second native call negotiated H.264, kept VideoToolbox hardware
encoding active, matched 1470x956 geometry, advanced video and bidirectional
voice counters without a continuity stall, and completed the bounded viewer
presentation recovery. GUI lifecycle passed all six probes. A separate
30-second diagnostic stopped advancing video late in the run and remains an
explicit partial result rather than a successful stability claim. See
[macOS Evidence Hardening Verification](../verification/macos-evidence-hardening.md).

### macOS Screen Stability Evidence

The macOS stability stage is delivered on `codex/macos-screen-stability`.
Long-run screen smoke can now own the deterministic motion fixture, guard it
for the complete measurement, clean it on every exit path, and record only
sanitized lifecycle booleans. The fixture explicitly raises and requests
activation at QML completion because a live but occluded process does not prove
changing screen content. ScreenCaptureKit, VideoToolbox, WebRTC, resolution,
cadence, bitrate, and queue policies were not changed.

On macOS 26.6.1 arm64, fixture-owned standard 30-second and 120-second runs
negotiated H.264, kept VideoToolbox active, matched 1470x956 geometry, advanced
video and bidirectional synthetic voice, and completed one bounded viewer
recovery. The 120-second run encoded 6810 host frames and decoded 6804 viewer
frames; host continuity had zero stalled observations and viewer continuity had
at most one. `call-dev` passed 51/51, `signaled_peer` repeated 20/20, and all
repository gates passed. See
[macOS Screen Stability Verification](../verification/macos-screen-stability.md).

The next evidence task is a **hardened Windows performance rerun**: collect
three independent software-baseline and three independent Media Foundation
candidate artifacts, then require the strict identities and continuous sample
window before accepting the comparison. The next product task remains the
**remaining Windows Screen and Voice Acceptance**: perform the two-device human
audio/visual acceptance pass, then close cursor composition and display
selection. Physical-display cadence and thermal evidence remain explicit
platform boundaries.
macOS physical-display and acoustic evidence may be completed in parallel as
environment permits.
File sharing, Movie Stage 2B, system-audio capture, HDR, remote input, TURN, and
4K60 optimization remain postponed and must not displace this direction.

### macOS Native Capture Restart Recovery

The macOS restart stage is merged on `main`. The host
now retains the same ref-counted screen source used by its WebRTC track, and a
bounded test probe can stop/start that source once without rebuilding signaling,
the peer connection, VideoToolbox selection, or voice tracks. Sanitized counters
prove the exact restart attempt, success, and generation; the smoke runner
records role-aligned recovery boundaries and owns safe fixture resume/cleanup.

On macOS 26.6.1 arm64, a 60-second standard call recreated ScreenCaptureKit
after 15 seconds while its dynamic fixture paused for three seconds. Restart
generation changed from zero to one in one sample; host and viewer video both
advanced one sample after resume, with 42 post-recovery samples remaining.
H.264 VideoToolbox stayed active, geometry matched at 1470x956, full-call video
stall maxima were one host observation and zero viewer observations,
bidirectional synthetic voice passed the bounded
continuity gate, and viewer presentation recovered exactly once. `call-dev`
passed 51/51, `signaled_peer` repeated 20/20, and repository gates passed. See
[macOS Screen Capture Restart Recovery](../verification/macos-motion-recovery.md).

The next macOS product stage is automatic bounded recovery after an unsolicited
`SCStreamDelegate` error, with retry limit, backoff, user-visible failure, and
the same frozen video/voice/quality gates. Actual sleep/wake, lock, permission
revocation, physical display/audio/temperature, Windows, and 4K remain separate
environment-dependent evidence.

### macOS Automatic Capture Recovery Policy

The automatic recovery stage is merged on `main`. Runtime
`screen-capture-stopped-*` categories on macOS now enter one bounded policy
episode with 250/500/1000 ms delays and at most three attempts. Recovery keeps
the existing PeerConnection, screen source, video track, VideoToolbox choice,
signaling, and voice tracks. Exhaustion enters the existing retryable result
page; the call UI displays “正在恢复屏幕共享” while controls and the last submitted
frame remain available.

A final 60-second controlled macOS run entered this policy, recreated
ScreenCaptureKit once, retained H.264 VideoToolbox at 1470x956, recovered both
peers in one sample, kept bidirectional synthetic voice continuous, and observed
42 post-recovery samples. Full `call-dev` CTest passed 52/52 and
`signaled_peer` repeated 20/20. Native callbacks now carry a generation so
retired stream events cannot affect the replacement stream, and early peer
failure/wait paths route recognized categories into recovery instead of
overwriting it with a terminal status. See
[macOS Automatic Screen Capture Recovery](../verification/macos-automatic-capture-recovery.md).

The accepted run used a private controlled policy trigger, not a physically
induced `SCStreamDelegate::didStopWithError`. Actual unsolicited delegate
failure, sleep/wake, lock, permission revocation, display removal, physical
audio/display/thermal evidence, Windows native rerun, and 4K remain partial or
environment-dependent. The next Mac evidence stage is a native delegate fault
seam or authorized physical fault campaign; do not describe the physical error
path as verified before that evidence exists.

### macOS Native Capture Delegate Fault Gate

The native delegate fault gate is delivered on
`codex/macos-native-capture-fault-gate`. Its macOS-only, opt-in diagnostic path
now invokes the active ScreenCaptureKit delegate's real
`stream:didStopWithError:` entry instead of entering recovery directly. The
normal controller error monitor discovers the sanitized category, performs the
existing bounded same-source recovery, and requires a successful real
`stopCapture` completion before starting the replacement stream.

After replacement, the runner invokes the retired delegate once. Its old
generation is rejected without setting a new error or beginning a second
recovery. Retained diagnostic state is released after stale injection, failed
native stop, failed replacement startup, shutdown, or controller cleanup. The
default path and Windows remain unchanged; video/voice tracks, signaling,
VideoToolbox, dimensions, cadence, bitrate, queues, and retry delays were not
changed.

On macOS 26.6.1 arm64, Apple M4, a 60-second standard native call passed the
controlled gate. H.264 VideoToolbox remained active at matching 1470x956;
attempt/success/generation changed exactly `0/0/0 -> 1/1/1`; both peers
recovered video in one sample; the retired delegate was injected after two
recovered samples; counters remained unchanged; 40 post-stale samples and
bidirectional synthetic voice continued. Final `call-dev` passed 52/52,
`signaled_peer` repeated 20/20, and independent final review found no Critical
or Important issue. See [macOS Native Capture Delegate Fault Gate](../verification/macos-native-capture-fault-gate.md).

This is verified controlled delegate invocation, not a claim that macOS
physically emitted an unsolicited error. Physical sleep/wake, lock/unlock,
permission revocation, display removal, audible voice, scanout, thermals,
Windows native rerun, and physical 4K remain environment-dependent. The next
Mac stage is an authorized physical sleep/wake and lock/unlock campaign while
preserving all current quality and voice gates.

## Active stage

Movie-audio transport isolation is merged on `main` through `2d806a5`.
Receiver native movie-audio playout is merged on `main` through `46710c7`.
Host-authoritative movie pause, resume, and seek remain the merged baseline
through `48e4d27`.
Sender local-track movie preview is merged on `main` through `b931a2f`.
Receiver playout reporting and generation-aware reconciliation are merged on
`main` through `ad1be7c`.

Delivered behavior:

- `MovieTimeline` is the sole synchronized absolute-PTS clock and increments
  generation exactly once per accepted seek;
- independent movie video and movie audio sessions obey the same state and
  generation, discard old queued media, and remain separate from voice paths;
- backward seeks preserve monotonic WebRTC transport timestamps;
- the host controller publishes timeline state/PTS/generation and exposes
  duration-bounded Pause/Resume/Seek QML controls;
- primary WebRTC transport owns video, bidirectional voice, and control only;
- a dedicated WebRTC runtime and PeerConnection carry one stereo movie-audio
  track with ADM recording disabled;
- the Qt viewer owns dedicated movie-audio playout through the bounded
  `MovieAudioRenderer` and `QtAudioOutputDevice`; `MovieAudioPeer` remains
  transport-only with `native_playout=false`, while voice remains on the
  existing primary WebRTC ADM path;
- the Qt host observes its own local WebRTC video track while the viewer
  observes the remote track, preventing the viewer's grayscale test source from
  replacing the sender's movie preview;
- the host publishes a same-frame movie PTS/RTP/generation anchor; the viewer
  reports actually submitted render frames, and both sides reject stale
  generations without applying an automatic correction;
- client and server signaling allowlists route dedicated SDP and ICE messages
  in the same room without changing primary relay types;
- builds without MovieRTC keep compiling and test a stable unsupported result.

See [Movie Audio Isolation Verification](../verification/movie-audio-isolation.md)
[Host Playback Controls Verification](../verification/host-playback-controls.md),
and [Receiver Playout Reports Verification](../verification/receiver-playout-reports.md)
for exact proof and evidence boundaries.

### Stage 2A checkpoint

The isolated branch `codex/movie-playback-stage2a` completed the final Stage 2A
review-fix round at `34fc3b9` (`fix: close final Stage 2A review gaps`). The
controller now owns renderer and scheduler lifecycle orchestration only: viewer
PCM callbacks use the renderer's bounded ingress, accepted media-scope changes
discard stale renderer/output PCM through stop/reopen/start on the owning
thread, remote pause/resume controls renderer output, Qt timers pump audio and
advance the observational scheduler, accepted host audio anchors use the
renderer logical-consumption origin, and existing playout reports publish
renderer/scheduler snapshots. Production video hold, drop, and hard-resync
application remain disabled.

Focused and full macOS verification passed: the deterministic 500 ms and 2 s
video-stall tests cover bounded held tokens, candidate and clock-blocked
telemetry, audio queue continuity, and provisional pass-through; configured
CTest passed 57/57; the registered CLI, drift-study, and performance-study
contracts passed; `git diff --check` and the portable-core forbidden-header scan
passed. Windows native media and live route/acoustic evidence remain
environment-dependent.

Correlation feasibility is `blocked-on-audio-correlation`: the locked
`RemoteAudioSource` API does not expose the sender media timestamp, and the
viewer callback has no shared source/decoded sequence. Anchors therefore remain
provisional and cannot authorize Stage 2B measurement or correction wiring.
Tasks 2B.2 and 2B.3 were not started. This implementation SHA is the Stage 2B
rollback point.

The final whole-Stage-2A review parked one defensive exception-path concern:
an injected `AudioOutputDevice::pause()` failure during a paused media-scope
restart could leave backend state uncertain, and failed resume is not retried
automatically. The concrete Qt adapter has no throwing pause path; the
renderer marks output inactive and clock state invalid on failure, and the
normal pause/resume, paused-scope reset, reopen-failure, and stale-PCM tests
pass. This parked edge case does not change the Stage 2B block or the platform
evidence boundary.

### Local Stage 2A Integration

The accepted Stage 1 handoff `32b7de4` is locally merged into `main` as
`c4ab791`. The accepted Stage 2A tip `c05fe48` is locally merged into `main` as
`202090a`. Fresh macOS verification on the merged trees passed Stage 1 CTest
`50/50` and Stage 2A CTest `57/57`; no push or deployment occurred.

Stage 2B is explicitly gated as `blocked-on-audio-correlation`. No shared
sender/decoded PCM correlation value or approved bounded-error estimator exists,
so no Audio Clock Gate measurement, experiment branch, correction wiring, or
Tasks 2B.1-2B.3 execution is authorized. Stage 3 was completed from this
rollback boundary; its accepted implementation tip is `a7afce1` and its local
merge is recorded below.

### Local Stage 3 Integration

The accepted Stage 3 branch `codex/movie-playback-stage3` is locally merged into
`main` as `41ce8ea` from implementation tip `a7afce1`. Tasks 3.1-3.4 deliver
portable route transactions, atomic renderer/device handoff, Qt and guarded
platform route monitors, controller route lifecycle integration, bounded
route-transition scheduler signaling, and sanitized diagnostics. Stage 2B
remains blocked and no estimator, Audio Clock Gate, correction, or hard resync
was added.

Fresh macOS Darwin arm64 verification passed movie-call CTest `59/59`, Qt/core
CTest `16/16`, direct CLI contracts `34/34`, the configured `shareme_rtc_demo`
build, the portable-core build, the portable-core forbidden-header scan, and
`git diff --check`. Qt/CoreAudio compilation and Qt offscreen route/output tests
are verified on macOS. Windows/Linux native route builds, live endpoint
switching, physical acoustic continuity, and native callback-failure injection
remain environment-dependent or unverified. Stage 3 rollback evidence is
Stage 2A merge `202090a`; no push or deployment occurred.

### Movie Playback Boundary Stage 1 Handoff

The safe video-path boundary is accepted on `codex/movie-playback-stage1` at
code SHA `96795e2`.

Verified on macOS arm64:

- the locked software movie-video default and explicit auto/fallback report;
- typed decoder-path propagation through `MediaInfo` and `MovieVideoFormat`;
- fixed WebRTC capability reporting as `vp8-software` with
  `unavailable-locked-abi` hardware status;
- separate sanitized `requested_mode`, `decoder_path`, `webrtc_encoder`, and
  `hardware_encoder_status` fields with fail-closed parser and artifact gates;
- fresh configure, build, focused CTest 25/25, full CTest 50/50, the three
  registered Stage 1 contract tests, Python performance-contract tests 11/11,
  and `git diff --check`;
- repository-external WebRTC checkout and depot-tools worktrees remained clean.

Selected VideoToolbox decoder behavior remains environment-dependent/partial:
the available fixture verified software and explicit-auto fallback, but not an
actual selected hardware decode. Windows native build/media evidence and all
hardware WebRTC encoding behavior remain unverified. The direct controller
performance-line contract test is a deferred non-blocking Minor from the
frozen Stage 1 test scope.

The next implementation boundary is Stage 2A from code SHA `96795e2`:
app-owned movie audio renderer, provisional audio clock/PTS contracts, and an
observational video scheduler. Correction remains candidate-only and Stage 2B
must not start unless the planned correlation checkpoint is satisfied.

### Current performance-stage handoff

On `codex/movie-playback-performance`, the quality-preserving performance
measurement and conversion-cleanup slice is complete with outcome
`blocked-on-quality-preserving-boundary`:

- the runner, sanitized JSONL counters, artifact aggregator, three-run 180-second
  software baseline, and three-run `auto` candidate are implemented and were
  executed on macOS with the supplied movie under the phase-correct scenario;
- direct FFmpeg-to-I420 frames and the bounded planar-YUV Qt preview adapter are
  implemented and covered by focused tests;
- movie senders explicitly request maintained framerate and resolution, and
  the Qt planar adapter holds ref-counted I420 planes instead of copying every
  frame; and
- macOS profiling established FFmpeg HEVC decode/conversion as a dominant host
  boundary, so an auto-only HEVC VideoToolbox path with software fallback was
  measured. It reached 30.04% combined average CPU reduction, -20.58% RSS P95
  growth, zero drops/coalescing, and host `path=hardware`, but failed the
  quality gate: worst viewer cadence was 70.20%, dimensions/metadata were not
  exact against the independent baseline, and PSNR/SSIM plus the paused probe
  were not recorded. The hardware path is therefore partial evidence, not a
  verified deliverable.
- the earlier 7.47% same-binary software/auto result is invalidated; the
  corrected evidence uses distinct baseline/candidate executable identities and
  measurement-window-only CPU/RSS samples for every run and role.
- final macOS regression verification passed CTest 50/50, including the MSVC
  portability regression contract, 20 repeated
  `signaled_peer` lifecycles, Go race/vet, workflow 8/8, the skill validator,
  and `git diff --check`; Windows and human visual/audio/thermal evidence remain
  environment-dependent or unperformed.
- the follow-up thermal investigation now makes the movie host video
  transceiver send-only and the viewer transceiver receive-only. The viewer no
  longer creates or encodes an unused outbound test-pattern track, while the
  host still renders its local movie preview and the viewer still receives the
  movie. A real peer integration regression verifies the directional contract.
  Experimental P010 pass-through, 10-bit conversion, and forced macOS H.264
  paths were rejected after either producing solid-green frames or failing the
  CPU/RSS gate; none of those hardware experiments remain in the committed
   source. The stage therefore remains `blocked-on-quality-preserving-boundary`.

The current working tree additionally moves diagnostics off synchronous Qt and
signaling-thread waits, protects concurrent media metrics, and preserves the
existing quality and drop policy. The final macOS binary completed three
strictly summarized 180-second software runs with 120 process and counter
samples per role in the measurement window, zero stats-unavailable samples,
zero coalescing/drops, and `max_pending=1`.

See [Movie Playback Performance Verification](../verification/movie-playback-performance.md).
The branch must not merge to `main` or resume drift/hard-resync work until the
remaining quality evidence and a quality-preserving performance boundary are
resolved.

## Verification status

- **Verified — macOS movie-call:** full build and CTest passed 50/50; the
  dedicated peer lifecycle test also passed 20 consecutive runs.
- **Verified — macOS live signaling:** five consecutive microphone/movie/audio
  smoke calls passed with stereo 48 kHz delivery and no captured codec
  collision or AudioSendStream race diagnostic.
- **Verified — signaling/workflow:** Go `-race`, Go vet, workflow 8/8, and the
  repository skill validator passed.
- **Verified — root-model choice and Terra routing:** project configuration
  does not pin the root model; Codex client/user configuration selects from
  models available to that session. Static workflow tests confirm the two-task
  cap, independent read-only parallel work, one-writer invariant, and
  Terra/medium role configuration. A fresh ShareMe task successfully
  dispatched `terra_explorer` at Terra/medium/read-only for a
  `client/rtc/desktop` source analysis without fallback or file changes.
  Codex did not expose an exact deployed model identifier; the configured
  model is recorded separately from that unobservable runtime detail.
- **Verified — cache preservation:** the repository-external Darwin arm64
  libwebrtc cache was used read-only; it was not cleaned, rewritten, or staged.
- **Verified — receiver sound:** the user confirmed native movie-audio output
  with the supplied 4K HEVC/FLAC movie on macOS.
- **Partial — sender preview GUI:** the local-track callback and role routing
  pass native tests, and a real-movie host/viewer GUI session connected without
  RTC error; exact on-screen movie content still needs human visual confirmation.
- **Verified — Windows automated screen path:** both native build suites and
  exact 1080p/1440p/4K Media Foundation H.264 local two-peer gates pass on the
  stated host. Two-device GUI/media and physical acoustic acceptance remain
  environment-dependent.
- **Verified — playout-report core:** macOS tests cover 32-bit RTP wrap,
  same-generation movie anchors, stale rendered-frame/report rejection, and
  host sync-decision observation.
- **Partial — playout telemetry GUI:** a supplied-movie host/viewer call stayed
  connected without RTC/media error; visible values and long-run drift remain
  human or instrumented acceptance.
- **Verified — drift-study tooling:** the Qt-free sample aggregator, sanitized
  JSONL writer, deterministic `drift-study-v1` scheduler, and fixed three-run
  runner contracts are covered by focused tests and the current macOS build.
- **Partial — drift-study measurement:** the required movie session reached the
  scripted 300-second host completion, but the formal run produced zero valid
  host-received playout reports. The same result reproduced once with Qt
  `offscreen` and once with native macOS Cocoa. The measurement chain now
  excludes the implicit no-report pause interval, wakes on viewer exit, and
  exposes sink/encode/send/receive counters; the post-fix native diagnostic
  recorded sink submissions but no report encode attempt. See
  [Movie Drift Study Verification](../verification/movie-drift-study.md).
- **Verified — macOS bounded movie diagnostics:** the final working-tree binary
  completed three sequential 180-second software runs. Strict aggregation
  accepted 120 process and counter samples per role in the measurement window;
  all runs recorded 358 counter records, zero stats-unavailable samples, zero
  coalescing/drops, and `max_pending=1`. Queue and owned-byte maxima are
  recorded in [Movie Playback Performance Verification](../verification/movie-playback-performance.md).
- **Partial — movie playback performance gate:** the counter chain and bounded
  ownership are verified, but no valid baseline/candidate quality comparison,
  PSNR/SSIM, paused probe, human GUI/audio confirmation, display scanout, or
  physical thermal evidence has been completed. This does not justify a
  CPU-reduction or temperature claim.
- **Unimplemented as an accepted delivery:** correction application, bounded
  hard resync, TURN/public network acceptance, process-loopback audio, a
  quality-passing platform hardware adapter, and all hard-resync tasks blocked
  by the failed drift gate.

## Next recommended stage

Proceed with the two-device Windows visual/audio acceptance run through the
complete GUI, then close cursor composition, display selection, and production
packaging planning. Preserve the verified hardware H.264,
host-video-send-only, viewer-video-receive-only, bidirectional primary voice,
exact actual-capture geometry, and bounded presentation recovery contracts.
Do not resume file sharing, Movie Stage 2B, reportability repair, or hard-resync
work while this primary product direction remains incomplete.

## Git handoff

- `codex/complete-gui` contains the reviewed product GUI and verification
  stage. Its generated builds, smoke JSONL, screenshots, caches, and local
  settings are ignored and must not be committed. Integration may proceed only
  after the final feature tip and merged `main` both pass their required gates.
- `codex/screen-voice-acceptance` was pushed at `9e2a23f` and fast-forwarded
  into `main` after merged-result CTest 39/39 and 64/64, Go race/vet, workflow
  8/8, skill validation, and clean-tree checks. The feature worktree and local
  branch may be removed after this integration handoff is pushed.
- `codex/movie-audio-isolation` and `codex/receiver-movie-audio-playout` are
  fully merged; their linked worktrees and local branches were removed after
  merged-main verification.
- Eleven uncommitted GUI-preview/debug changes found in the older isolation
  worktree were not suitable for `main` and were preserved in the named local
  stash `archive: movie-audio GUI and debug WIP before worktree cleanup`.
- The Sol–Terra migration is an unmerged, unpushed focused branch with
  separate design, plan, RED-test, policy, fresh-route verification, and
  handoff commits.
- The repository-external libwebrtc cache was preserved and used read-only.
- Keep Windows results environment-dependent until that machine reruns the
  affected configuration.
- `codex/movie-playback-performance` remains an unmerged, unpushed active
  worktree with uncommitted source/test changes for nonblocking diagnostics and
  concurrent media metrics.
- `codex/movie-playback-stage2a` contains the accepted Stage 2A implementation
  at `34fc3b9`; its detailed evidence is in
  `.superpowers/sdd/2026-08-05-movie-playback-three-stage/task-2A.6-report.md`.
- The latest ignored macOS artifacts are under
  `build/movie-call-dev/movie-performance-diagnostics-20260805-queue-rerun8`;
  they are evidence only and are not part of Git.
