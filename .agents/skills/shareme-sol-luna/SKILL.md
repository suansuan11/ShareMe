---
name: shareme-sol-luna
description: Use when working in the ShareMe repository on implementation, diagnosis, review, testing, planning, release, risky cleanup, or continuation requests such as “继续开发”
---

# ShareMe Sol-Luna

## Core principle

Own one verifiable stage from current evidence through review, tests, and a focused handoff. Luna output is evidence; Sol owns every decision and completion claim.

## Required workflow

1. Read `AGENTS.md`, `docs/development/current-stage.md`, relevant current code, tests, plans, verification evidence, and Git state. Verify freshness in the owned checkout; treat other worktrees as unrelated user state.
2. Classify the request as answer, diagnosis, change, review, risky operation, or continuation. Read [project-contract.md](references/project-contract.md) whenever a claim touches architecture, platform, media, Git, cache, or verification boundaries.
3. Define a verifiable stage with acceptance evidence. Do not stop after exploration or an internal subtask.
4. Choose direct Sol work or one bounded Luna role. Read [role-contracts.md](references/role-contracts.md) before every dispatch.
5. Use TDD for behavior changes. Use an ignored feature worktree for a substantive stage.
6. Before integration, apply the specification-compliance gate, then the code-quality gate. Fix Critical and Important findings.
7. Run focused tests, affected suites, `git diff --check`, and platform acceptance proportional to the claim. Label outcomes verified, partial, environment-dependent, or unimplemented.
8. Create focused commits. Update the current-stage handoff only at a stage boundary or future-changing decision. Push, merge, and cleanup require existing authorization; verify a pushed remote ref.

## Quick reference

| Situation | Action |
|---|---|
| Small, sequential, tightly coupled work | Sol works directly |
| Bounded read-only discovery | Luna Explorer; at most two independent readers |
| Independent bounded implementation | One Luna Implementer; no parallel implementation |
| Independent verification | Source-read-only Tester/Reviewer only with explicit ignored build-output scope |
| Architecture, security, deletion, concurrency, lifetime, consistency | Sol High before implementation |
| “继续开发” | Resume the recorded next stage through delivery |

## Observed rationalizations

| Rationalization | Required response |
|---|---|
| “Infer the next stage from nearby docs.” | Reconcile the canonical handoff with current code, tests, evidence, and Git. |
| “More writers are faster.” | Keep overlapping writers at zero and reviewers read-only. |
| “Delete the external cache to make room.” | Inspect purpose and recoverability; preserve it absent explicit authorization. |
| “A nearby worktree has fresher context.” | Use only the owned checkout unless that worktree is explicitly in scope. |

## Common mistakes

- Treating README history or macOS results as current Windows evidence.
- Reporting a Luna response, skipped suite, or source inspection as completion.
- Mixing generated, cache, secret, or unrelated files into a commit.

## Red flags

Stop and correct the plan if it has four agents, concurrent writers, a fixing reviewer, cache deletion without authorization, cross-worktree context, stale platform claims, or a small-task stopping point.
