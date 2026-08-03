# ShareMe Current Development Stage

This is the canonical dynamic handoff. Verify it against current Git, source,
tests, and verification evidence before relying on it.

## Delivered baseline

- `main` includes the receiver playback-state channel through merge `87138a9`.
- The simplified repository workflow is merged on `main` through `834c917`.
  This corrects the previous stale claim that the workflow branch was awaiting
  authorization. Root `AGENTS.md`, the ShareMe skill, Sol/Luna role files, and
  the one-child configuration are active for new trusted ShareMe tasks.
- The deterministic ShareMe workflow stage is delivered on
  `codex/shareme-workflow-simplify` and integrated by that merge. The remote
  branch remains historical backup evidence, not pending work.
- Windows movie/microphone regression and Desktop Duplication evidence remain
  recorded in their linked verification documents; this macOS stage does not
  replace or extend those Windows claims.

## Active stage

Movie-audio transport isolation is implemented on
`codex/movie-audio-isolation` and is ready for integration after automated
macOS verification. Host-authoritative movie pause, resume, and seek remain the
merged baseline through `48e4d27`.

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
- **Verified — cache preservation:** the repository-external Darwin arm64
  libwebrtc cache was used read-only; it was not cleaned, rewritten, or staged.
- **Partial — GUI:** QML compiled and control bindings have an automated
  contract, but no human visual acceptance is claimed for this run.
- **Environment-dependent — Windows:** rerun native movie-call build/tests and
  GUI/media acceptance after pulling the merged `main`.
- **Unimplemented:** viewer playout reports, generation-aware receiver buffer
  reconciliation, bounded hard resync, receiver speaker playout, TURN/public
  network acceptance, process-loopback audio, and measured performance.

## Next recommended stage

After a manual macOS GUI rerun, add receiver playout reports and generation-aware reconciliation before a
bounded host hard-resync command. This supplies the missing evidence and buffer
semantics needed to correct drift without faking synchronization. Keep receiver
speaker playout as a separate audio-lifecycle stage.

## Git handoff

- The movie-audio isolation branch is not yet merged or pushed. Its focused
  commits and final verification commit are the current handoff boundary.
- The repository-external libwebrtc cache was preserved and used read-only.
- Keep Windows results environment-dependent until that machine reruns the
  affected configuration.
