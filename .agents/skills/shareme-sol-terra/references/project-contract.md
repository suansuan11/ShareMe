# ShareMe Project Contract

Read this contract before making or reviewing architecture, platform, media, Git, cache, or verification claims. Current code, tests, verification documents, and Git state outrank historical summaries.

## Platform evidence

ShareMe is a Windows-first product. Record macOS and Windows evidence separately. A build, test, or probe on one platform never verifies the other. State the exact platform and evidence scope; do not turn portable-core or source-level evidence into native media verification.

## Portable core

Keep `client/core` portable C++20. It must not depend on Qt, FFmpeg, libwebrtc, GPU SDKs, D3D11, WASAPI, or operating-system headers. Put platform and third-party adapters outside this boundary.

## Media paths

Keep movie audio, host voice, and viewer voice as independent paths. Do not combine their lifecycle, queue, device, synchronization, or verification claims merely because they share transport or UI.

## Queue and timing contracts

Queues remain bounded and observable. Before changing drop/reject policy, capacity, latency thresholds, clocking, or synchronization behavior, capture measurements and update the associated contract, tests, and verification evidence. Do not present an unmeasured timing change as an improvement.

## External libwebrtc cache

Preserve repository-external libwebrtc checkout, build, and cache content by default. Before proposing cleanup, inspect its purpose, rebuild cost, ownership, recoverability, and exact scope without exposing machine-specific paths. Delete only with explicit user authorization after those facts are known.

A user-level filesystem-read-only or "Do not edit" request authorizes zero filesystem mutations, including deletion or creation of generated build output. A request to free disk space is not deletion authorization; in that task, report candidates and evidence only. Never bypass a rejected destructive command with a different deletion mechanism. Conflicting wording must resolve toward preservation and explicit confirmation, especially for repository-external dependencies.

Source-read-only test execution may write only explicitly allowed ignored build output whose exact root is named in the dispatch; it never authorizes source, document, cache, dependency, or unrelated generated-output changes. "Do not edit" overrides source-read-only test execution and forbids even build-output writes.

## Repository hygiene

Exclude build trees, generated media or fixtures, dependency caches, secrets, local configuration, IDE state, and unrelated user files from commits. Inspect Git state before staging. Work only in the owned checkout; another linked worktree is unrelated state unless explicitly placed in scope.

## Stage delivery and Git

Use an ignored feature worktree for substantive stages. Complete the agreed acceptance target, affected verification, review gates, and focused commits before stopping; exploration or one internal task is not a stage result. Keep commits coherent and reversible. Push, merge, deployment, and cleanup require existing authorization. After pushing, verify the intended remote ref rather than assuming delivery.

Compute numerical Git claims from exact current commands such as `git rev-list --count` and `git diff --stat`; never infer counts from a narrative or hand-count a log. Name the compared refs with every count.

## Evidence labels

- **Verified:** The stated behavior passed named, current evidence on the claimed platform and scope.
- **Partial:** Some acceptance evidence passed, but a stated portion remains unchecked or incomplete.
- **Environment-dependent:** Required evidence cannot run in the current environment; name the missing platform, hardware, service, or tool.
- **Unimplemented:** Required behavior does not exist; plans, stubs, or source inspection do not change this label.

Skipped tests, static inspection, and single-platform probes cannot establish end-to-end or cross-platform completion.
