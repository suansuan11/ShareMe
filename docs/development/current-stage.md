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
Tasks 2B.1-2B.3 execution is authorized. The next authorized task is Stage 3
Task 3.1, with `c05fe48` and merge `202090a` as rollback evidence.

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
- **Environment-dependent — Windows:** rerun native movie-call build/tests and
  GUI/media acceptance after pulling the merged `main`.
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

Complete the remaining quality-preserving performance gate for the bounded
diagnostics stage: a distinct baseline/candidate comparison, same-timestamp
PSNR/SSIM samples, a paused probe, human GUI/audio acceptance, and Windows
reruns. Do not lower quality, tune thresholds, resume viewer reportability
repair, or start hard-resync until the frozen performance gate is genuinely
satisfied. Windows validation remains a separate, environment-dependent stage.

## Git handoff

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
