# ShareMe Current Development Stage

This file is the canonical dynamic handoff for future ShareMe sessions. Verify
its claims against current Git, code, tests, and verification evidence before
relying on them; those current sources outrank this summary when they diverge.

## Delivered baseline

- The delivered product baseline now includes receiver-control implementation
  commit `92060e5` (`feat: add receiver playback state channel`) on the active
  feature branch. The existing desktop source and tests live under
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
- **Reconciled — README status table:** the active stage updates the
  [README verification table](../../README.md#验证状态) to reflect the newer
  Windows evidence and keeps the remaining gaps explicit.
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
- **Historical — dynamic Luna routing:** one dated explicit-override audit was
  accepted, but it did not configure default routing or prove savings. It is
  superseded by the deterministic configuration; see
  [Dynamic Luna Model Routing Verification](../verification/shareme-dynamic-luna-routing.md).
- **Verified — deterministic workflow configuration:** the repository now
  supplies Sol/medium defaults, Luna/medium child defaults, a one-child limit,
  and two project-scoped Luna role files. On macOS, the workflow contract passed
  8/8, the repository skill validator reported `Skill is valid!`, the portable
  C++ suite passed 6/6, and Go `-race` plus `go vet ./...` passed. The bundled
  desktop Codex parsed the project configuration without a
  project-configuration error.
  **Environment-dependent:** strict validation is blocked by an unrelated
  unknown field in the user-level Playwright configuration; this stage does not
  modify that global file. **Unmeasured:** realized credit saving has no
  comparable per-agent usage telemetry. New-session activation requires a
  fresh trusted ShareMe task and is not yet observed in this stage.
- **Verified — receiver control automation:** the macOS movie-call build and
  complete CTest suite passed 37/37, including real two-peer data-channel
  delivery and playback-state validation. Go race/vet, workflow 7/7, and the
  skill validator also passed. See
  [Player / Receiver Control Verification](../verification/player-receiver-control.md).
- **Partial — local GUI process acceptance:** host and viewer joined the same
  room and remained healthy, but visual capture was unavailable. Do not claim
  that this run visually observed the remote frame or QML state label.

## Active stage

Repository automation is delivered on `main` through merge commit `9c9eb19`:
root `AGENTS.md` activates the repository-owned ShareMe skill, bounded Sol-Luna
roles, exact review/test permissions, cache safeguards, and this cross-session
handoff. The merged result passed the complete verification listed above. This
stage changes no product behavior.

The historical dynamic-routing stage remains on `main` through merge commit
`ac118c4`, but it is superseded for future ShareMe tasks by the deterministic
configuration described above.

The deterministic ShareMe workflow stage is delivered on
`codex/shareme-workflow-simplify` through commits `46b41fb`, `e043bcd`, and
`1e7f9de`, followed by its documentation handoff. It adds no product behavior
or new platform acceptance. Push and merge remain outside the current
authorization.

The receiver-control product stage is delivered on `main` through merge commit
`87138a9`. It adds host movie selection to the Qt RTC demo plus a reliable
ordered playback-state channel and read-only receiver display. Its final
specification and quality reviews approved with no remaining findings.

## Next recommended stage

The recommended next product stage is host-authoritative pause/seek: add
controllable movie-source timeline semantics,
increment generation on seek, and reconcile the viewer through playback-state
plus a bounded hard-resync command. Keep receiver speaker playout as a separate
audio-lifecycle stage.

Windows process-loopback audio, TURN operation, measured performance, and
multi-machine acceptance remain explicit follow-ups. Keep each labeled
environment-dependent or unimplemented until current evidence proves its
claimed platform and scope.

## Git handoff

- The dynamic-routing feature ref and `main` merge were pushed and verified.
  Its owned worktree and local feature branch were removed; the remote feature
  branch remains as a stage backup.
- The workflow feature and merge refs were pushed and verified. The owned
  workflow worktree and local feature branch were removed; the remote feature
  branch remains as a stage backup.
- Future sessions must rely on the committed root `AGENTS.md`, not an old
  worktree. New sessions discover it automatically. A session opened before
  the workflow commit may require reopening the repository or the one-time
  instruction `重新加载 ShareMe 工作流后继续`; repository files cannot guarantee
  hot reload in an already-running external session.
- Keep workflow commits focused and preserve unrelated files and the external
  libwebrtc cache.
- The deterministic-workflow branch is in an ignored local worktree. Its
  configuration applies to future trusted ShareMe tasks; it does not change an
  already-running task or global Codex settings.
