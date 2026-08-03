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
- **Verified — repository workflow:** the workflow contract passed 5/5, the
  skill validator reported `Skill is valid!`, the portable C++ suite passed
  6/6, and the signaling server passed `go test -count=1 -race ./...` plus
  `go vet ./...`. Independent specification and workflow-quality reviews both
  approved the final contract with no remaining findings.
- **Verified — preserved dependency cache:** the repository-external Darwin
  arm64 libwebrtc cache was restored and checked at locked revision
  `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`, including all required nonempty
  archives. Its live state must still be rechecked before future cache claims
  or cleanup decisions.

## Active stage

Repository automation is delivered on the feature branch and ready for
integration: root `AGENTS.md` activates the repository-owned ShareMe skill,
bounded Sol-Luna roles, exact review/test permissions, cache safeguards, and
this cross-session handoff. This stage changes no product behavior.

## Next recommended stage

After the workflow is delivered, first reconcile the stale README verification
table with the newer Windows evidence. Then resume player integration and
receiver playback control as the next product stage.

Windows process-loopback audio, TURN operation, measured performance, and
multi-machine acceptance remain explicit follow-ups. Keep each labeled
environment-dependent or unimplemented until current evidence proves its
claimed platform and scope.

## Git handoff

- Workflow integration is authorized after both review gates and complete
  feature-branch verification pass. Verify the pushed feature and `main` refs,
  then remove only the owned workflow worktree and local feature branch.
- Future sessions must rely on the committed root `AGENTS.md`, not an old
  worktree. New sessions discover it automatically. A session opened before
  the workflow commit may require reopening the repository or the one-time
  instruction `重新加载 ShareMe 工作流后继续`; repository files cannot guarantee
  hot reload in an already-running external session.
- Keep workflow commits focused and preserve unrelated files and the external
  libwebrtc cache.
