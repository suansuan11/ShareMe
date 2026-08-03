# ShareMe Current Development Stage

This file is the canonical dynamic handoff for future ShareMe sessions. Verify
its claims against current Git, code, tests, and verification evidence before
relying on them; those current sources outrank this summary when they diverge.

## Delivered baseline

- The delivered product baseline is commit `5db696c` (`feat: implement Windows
  desktop sharing`). The current desktop source and tests live under
  [`client/rtc/desktop`](../../client/rtc/desktop/) and
  [`tests/rtc`](../../tests/rtc/).
- Windows Desktop Duplication hardware capture and the local two-process path
  are recorded as verified in
  [Windows Desktop Duplication Verification](../verification/windows-desktop-duplication.md).
- The Windows movie, independent movie-audio, and microphone regression is
  recorded in
  [Windows Cross-Platform Regression Verification](../verification/windows-cross-platform-regression.md).

## Verification status

- **Verified — Windows:** the linked Desktop Duplication evidence covers real
  hardware capture and local two-process desktop delivery. The linked
  cross-platform regression covers the Windows movie and microphone paths.
- **Verified — macOS baseline:** this workflow worktree inherits the recorded
  portable-core 6/6 result plus Go race tests and `go vet ./...` from the
  [main delivery verification](../verification/signaled-movie-audio.md#main-delivery-verification).
- **Stale — README status table:** the
  [README verification table](../../README.md#验证状态) predates the newer
  Windows evidence above. In particular, its Windows Qt/FFmpeg and libwebrtc
  rows are stale and must not be treated as current truth.
- This repository-automation stage changes workflow documents and their
  contract test, not product behavior. It adds no new platform acceptance;
  missing platform-specific reruns remain environment-dependent rather than
  silently verified.

## Active stage

Repository automation implementation is active: automatic ShareMe workflow
discovery, bounded Sol-Luna roles, and this cross-session handoff are being
delivered on the workflow branch. Acceptance requires the workflow unit test,
the skill validator, and a clean diff check.

## Next recommended stage

After the workflow is delivered, first reconcile the stale README verification
table with the newer Windows evidence. Then resume player integration and
receiver playback control as the next product stage.

Windows process-loopback audio, TURN operation, measured performance, and
multi-machine acceptance remain explicit follow-ups. Keep each labeled
environment-dependent or unimplemented until current evidence proves its
claimed platform and scope.

## Git handoff

- Branch: `codex/shareme-sol-luna-workflow`.
- Owned worktree: `.worktrees/shareme-sol-luna-workflow`.
- Keep workflow commits focused and exclude unrelated files.
- Do not push or merge until the workflow gates pass; push and merge also
  require user authority under the repository workflow.
