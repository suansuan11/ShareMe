---
name: shareme-sol-terra
description: Use when working in the ShareMe repository on implementation, diagnosis, review, testing, planning, release, risky cleanup, or continuation requests such as “继续开发”
---

# ShareMe Sol-Terra

## Direct-first workflow

1. Inspect current Git, relevant source, tests, and the current-stage handoff.
   Read [project-contract.md](references/project-contract.md) before claims
   about architecture, platform, media, Git, cache, or verification.
2. Define one verifiable stage with acceptance evidence. Preserve unrelated
   worktrees and user changes.
3. Sol works directly by default. Use at most two independent Terra agents only
   for bounded work that materially improves focus or elapsed time. Parallel
   work must be independent read-only exploration, testing, or log analysis;
   read [role-contracts.md](references/role-contracts.md) before dispatching.
4. Use TDD for behavior changes and an ignored worktree for substantive stages.
   One writer owns an overlapping implementation scope; dependent review or
   testing starts after its write phase.
5. Run focused tests, affected suites, `git diff --check`, and platform
   acceptance proportional to the claim. Label results verified, partial,
   environment-dependent, or unimplemented.
6. Create focused commits after checking Git state and the staged diff. Update
   the handoff only at a stage boundary. Push, merge, deployment, and cleanup
   require existing user authority.

## Guardrails

- Preserve repository-external libwebrtc caches unless the user explicitly
  authorizes deletion after the project-contract checks.
- Do not treat source inspection, skipped tests, or another agent's summary as
  end-to-end proof.
- Routine small changes, ordinary tests, formatting, and review stay with Sol.
  Do not create a subagent merely to save credits.
