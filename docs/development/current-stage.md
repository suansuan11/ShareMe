# ShareMe Current Development Stage

This is the canonical dynamic handoff. Verify it against current Git, source,
tests, and verification evidence before relying on it.

## Delivered baseline

- `main` includes the receiver playback-state channel through merge `87138a9`.
- The simplified repository workflow is merged on `main` through `834c917`.
  This corrects the previous stale claim that the workflow branch was awaiting
  authorization. Root `AGENTS.md`, the ShareMe skill, Sol/Luna role files, and
  the one-child configuration are active for new trusted ShareMe tasks.
- Windows movie/microphone regression and Desktop Duplication evidence remain
  recorded in their linked verification documents; this macOS stage does not
  replace or extend those Windows claims.

## Active stage

Host-authoritative movie pause, resume, and seek are complete on
`codex/host-playback-controls` through implementation commit `acc82d0` plus the
optional-build contract repair `edbe621` and final timeline alignment repair
`e19e15d`, pending final documentation and Git integration.

Delivered behavior:

- `MovieTimeline` is the sole synchronized absolute-PTS clock and increments
  generation exactly once per accepted seek;
- independent movie video and movie audio sessions obey the same state and
  generation, discard old queued media, and remain separate from voice paths;
- backward seeks preserve monotonic WebRTC transport timestamps;
- the host controller publishes timeline state/PTS/generation and exposes
  duration-bounded Pause/Resume/Seek QML controls;
- builds without MovieRTC keep compiling and test a stable unsupported result.

See [Host Playback Controls Verification](../verification/host-playback-controls.md)
for exact proof and evidence boundaries.

## Verification status

- **Verified — macOS movie-call:** full build and CTest passed 38/38.
- **Verified — signaling/workflow:** Go `-race`, Go vet, workflow 8/8, and the
  repository skill validator passed.
- **Verified — cache preservation:**
  `/Users/dio/Library/Caches/ShareMe/webrtc` was used read-only; it was not
  cleaned, rewritten, or staged.
- **Partial — GUI:** QML compiled and control bindings have an automated
  contract, but no human visual acceptance is claimed for this run.
- **Environment-dependent — Windows:** rerun native movie-call build/tests and
  GUI/media acceptance after pulling the merged `main`.
- **Unimplemented:** viewer playout reports, generation-aware receiver buffer
  reconciliation, bounded hard resync, receiver speaker playout, TURN/public
  network acceptance, process-loopback audio, and measured performance.

## Next recommended stage

Add receiver playout reports and generation-aware reconciliation before a
bounded host hard-resync command. This supplies the missing evidence and buffer
semantics needed to correct drift without faking synchronization. Keep receiver
speaker playout as a separate audio-lifecycle stage.

## Git handoff

- Finish the current feature documentation commit, push the feature branch,
  merge it into a clean `main`, rerun final gates on merged `main`, and push.
- After integration, replace this feature-branch handoff with the actual merge
  commit and final remote-ref evidence.
- Preserve the repository-external libwebrtc cache and remote feature branch;
  remove only the completed ignored worktree and local feature branch.
- Keep Windows results environment-dependent until that machine reruns the
  affected configuration.
