# ShareMe Current Development Stage

This is the canonical dynamic handoff. Verify it against current Git, source,
tests, and verification evidence before relying on it.

## Delivered baseline

- `main` includes the receiver playback-state channel through merge `87138a9`.
- The simplified repository workflow is merged on `main` through `834c917`.
  This corrects the previous stale claim that the workflow branch was awaiting
  authorization. Root `AGENTS.md`, the ShareMe skill, Sol/Luna role files, and
  the two-child configuration are active for new trusted ShareMe tasks.
- The two-Luna policy refinement is delivered on
  `codex/shareme-luna-parallelism`: the project cap is two independent Luna
  tasks, Luna/medium role configuration is unchanged, and one writer remains
  mandatory for every implementation scope.
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
- client and server signaling allowlists route dedicated SDP and ICE messages
  in the same room without changing primary relay types;
- builds without MovieRTC keep compiling and test a stable unsupported result.

See [Movie Audio Isolation Verification](../verification/movie-audio-isolation.md)
and [Host Playback Controls Verification](../verification/host-playback-controls.md)
for exact proof and evidence boundaries.

## Verification status

- **Verified — macOS movie-call:** full build and CTest passed 39/39; the
  dedicated peer lifecycle test also passed 20 consecutive runs.
- **Verified — macOS live signaling:** five consecutive microphone/movie/audio
  smoke calls passed with stereo 48 kHz delivery and no captured codec
  collision or AudioSendStream race diagnostic.
- **Verified — signaling/workflow:** Go `-race`, Go vet, workflow 8/8, and the
  repository skill validator passed.
- **Verified — Luna parallelism policy:** static workflow tests confirm the
  two-task cap, independent read-only parallel work, and one-writer invariant.
  Actual Luna dispatch remains **Environment-dependent**: a runtime that
  rejects Luna must not silently substitute Terra or be treated as a successful
  Luna configuration check.
- **Verified — cache preservation:** the repository-external Darwin arm64
  libwebrtc cache was used read-only; it was not cleaned, rewritten, or staged.
- **Partial — GUI:** the supplied 4K HEVC/FLAC movie kept a macOS host/viewer
  GUI session connected with native output initialization and no RTC error;
  audible speaker confirmation remains a human acceptance step.
- **Environment-dependent — Windows:** rerun native movie-call build/tests and
  GUI/media acceptance after pulling the merged `main`.
- **Unimplemented:** viewer playout reports, generation-aware receiver buffer
  reconciliation, bounded hard resync, TURN/public network acceptance,
  process-loopback audio, and measured performance.

## Next recommended stage

After manual audible macOS confirmation, add receiver playout reports and
generation-aware reconciliation before a bounded host hard-resync command.
This supplies the missing evidence and buffer semantics needed to correct drift
without faking synchronization.

## Git handoff

- `codex/movie-audio-isolation` has no unique commits relative to `main`; it is
  fully merged and retained only as a linked historical worktree.
- `codex/receiver-movie-audio-playout` has been integrated through merge
  `46710c7`; the linked branch remains local stage evidence.
- The Luna parallelism refinement is an unmerged, unpushed focused branch with
  separate RED-test, policy, and handoff commits. Keep its runtime dispatch
  evidence environment-dependent until a fresh ShareMe task accepts Luna.
- The repository-external libwebrtc cache was preserved and used read-only.
- Keep Windows results environment-dependent until that machine reruns the
  affected configuration.
