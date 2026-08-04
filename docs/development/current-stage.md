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
- the Qt viewer uses a native playout-only ADM for the dedicated movie track;
  host and headless probe paths retain the deterministic discard renderer;
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

## Verification status

- **Verified — macOS movie-call:** full build and CTest passed 40/40; the
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
- **Unimplemented:** correction application, bounded hard resync, TURN/public
  network acceptance, process-loopback audio, and measured performance.

## Next recommended stage

Measure playout-report drift over pause/resume, forward/backward seek, and a
bounded long-running movie sample. Use that distribution to define and test the
first bounded hard-resync command without changing the existing correction
thresholds by assumption.

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
