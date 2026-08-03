---
name: shareme-sol-luna
description: Use when working in the ShareMe repository on implementation, diagnosis, review, testing, planning, release, risky cleanup, or continuation requests such as “继续开发”
---

# ShareMe Sol-Luna

## Direct-first workflow

1. Inspect current Git, relevant source, tests, and the current-stage handoff.
   Read [project-contract.md](references/project-contract.md) before claims
   about architecture, platform, media, Git, cache, or verification.
2. Define one verifiable stage with acceptance evidence. Preserve unrelated
   worktrees and user changes.
3. Sol works directly by default. Use at most one active Luna only for a
   bounded task that materially improves focus or elapsed time; read
   [role-contracts.md](references/role-contracts.md) before dispatching.
4. Use TDD for behavior changes and an ignored worktree for substantive stages.
   One writer owns an overlapping implementation scope.
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
