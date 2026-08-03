# ShareMe Sol–Luna Workflow Design

## Goal

Create a repository-owned development workflow that makes every ShareMe Codex
session start from the same project state, delegate work through bounded roles,
and stop only after a verifiable stage result. The workflow must travel through
Git to macOS and Windows without hard-coded model names or machine paths.

## Scope

This change configures how agents develop ShareMe. It does not change product
code, build dependencies, runtime configuration, or the global Codex config.

The source prompt is the externally provided `Sol–Luna分层开发.md`. Its role
model is adapted to ShareMe's existing worktree, TDD, review, verification,
and focused commit practices. The source file itself is not copied into the
repository.

## Repository Layout

```text
AGENTS.md
.agents/skills/shareme-sol-luna/
  SKILL.md
  agents/openai.yaml
  references/
    project-contract.md
    role-contracts.md
docs/development/current-stage.md
```

`AGENTS.md` is the automatic repository entry point. It stays concise and
requires agents working in this repository to read the project skill and the
current-stage document before planning or changing ShareMe.

`SKILL.md` owns the procedural workflow. Stable ShareMe architecture and
platform boundaries live in `project-contract.md`; delegation request and
return shapes live in `role-contracts.md`. Dynamic handoff state lives outside
the skill in `docs/development/current-stage.md` so it can change without
turning the skill into a project diary.

No `.codex/config.toml`, model override, hook, MCP dependency, generated backup
directory, or global `~/.codex` installation is added.

## Automatic Use and Session Continuity

New sessions opened inside the repository discover `AGENTS.md`, which directs
them to the project skill. The skill description also covers implementation,
diagnosis, review, planning, testing, release, and continuation requests such
as “继续开发”.

The current session adopts the workflow immediately after installation. A
runtime may not hot-reload repository instructions or newly installed skills
inside another session that was already open before the commit. Such a session
must be reopened or receive one explicit “重新加载 ShareMe 工作流后继续” request.
The repository cannot safely override this runtime limitation.

`current-stage.md` is the cross-session handoff contract. Before substantive
work, Sol verifies its claims against Git, current code, tests, and verification
documents. The file records only:

- current delivered baseline and commit;
- active stage and acceptance target;
- verified, partial, environment-dependent, and excluded outcomes;
- owned branch/worktree state;
- next recommended stage;
- last verification evidence.

It is updated at stage boundaries or when a decision changes future work, not
after every internal task.

## Role Model

### Sol

The root agent is always Sol. Sol owns requirement interpretation, task shape,
risk classification, architecture decisions, delegation, diff review,
verification, Git integration, documentation accuracy, and the final claim.
Subagent output is evidence to inspect, not completion by itself.

### Luna Explorer

Use for bounded read-only exploration: repository structure, call chains,
protocols, logs, tests, and current evidence. It may not edit files, add
dependencies, commit, or refactor.

### Luna Implementer

Use only after Sol defines a concrete target, allowed files, prohibited scope,
acceptance criteria, tests, rollback, and return format. Only one implementer
may write an overlapping area. It may not expand requirements or perform
unrelated cleanup.

### Luna Tester or Reviewer

Use as a read-only independent gate. It builds, tests, inspects the diff, and
reports exact failures. It may not repair implementation during the review.

Luna is optional. Sol handles small, sequential, or tightly coupled work
directly when delegation would add more coordination than evidence. Default
parallelism is at most two Luna agents and never exceeds three. Parallel work
is limited to independent read-only exploration or independent verification;
multiple writers may not touch the same repository state.

## Sol High Escalation

Escalate before implementation when work changes a long-term architecture or
protocol, performs irreversible data migration or deletion, changes security
or permissions, or risks concurrency, lifetime, or consistency guarantees.
Escalation means deeper analysis, explicit alternatives, and user approval
where authority or product direction changes. Search, routine testing, small
bug fixes, formatting, and documentation cleanup do not qualify.

## Development Flow

1. Read `AGENTS.md`, this skill, `current-stage.md`, relevant plans, current
   code, tests, and Git state.
2. Classify the request as answer, diagnosis, implementation, review, risky
   operation, or stage continuation.
3. Define one verifiable stage. Do not report exploration or a small internal
   task as a stage result.
4. For behavior changes, use TDD. For a multi-step stage, write or update a
   design and implementation plan before code.
5. Use an ignored `.worktrees/` feature worktree for substantive stages. Keep
   generated media, build output, caches, local configuration, and unrelated
   user files out of commits.
6. Delegate only through the role contract. Sol reviews every returned diff
   and test claim.
7. Apply two gates to substantial implementation: specification compliance,
   then code quality. Fix Critical and Important findings before proceeding.
8. Run focused tests, affected full suites, `git diff --check`, and platform
   acceptance proportional to the claim. Distinguish verified, partial,
   environment-dependent, and unimplemented results.
9. Create focused commits at coherent boundaries. Push, merge, or clean up only
   within existing user authorization; verify the remote ref after pushing.
10. Update `current-stage.md` and verification documents at the stage boundary.

“继续开发” means resume the recorded next stage and continue through tests,
review, focused commits, and delivery. It does not authorize unrelated product
scope, destructive cleanup, deployment, or claims for an untested platform.

## ShareMe-Specific Guardrails

- Windows remains the primary native target. macOS evidence must not be stated
  as Windows verification, and vice versa.
- `client/core` remains portable C++20 and cannot depend on Qt, FFmpeg,
  libwebrtc, GPU SDKs, D3D11, WASAPI, or operating-system headers.
- Movie audio, host voice, and viewer voice remain independent paths.
- Queue and timing policy changes require measurements and contract updates.
- Preserve repository-external libwebrtc caches unless the user explicitly
  authorizes cleanup after their purpose and recoverability are established.
- Never commit build trees, generated fixtures, secrets, local IDE state,
  caches, or unrelated files.
- Never turn a skipped test, source inspection, or single-platform probe into
  an end-to-end or cross-platform completion claim.

Git worktrees and focused commits provide rollback. The workflow does not
create `.codex-backup/`, because that would duplicate tracked content and add
repository noise.

## Delegation Contract

Every Luna task contains these fields:

```text
Role:
Goal:
Allowed scope:
Forbidden scope:
Context and evidence:
Acceptance:
Commands/tests:
Rollback:
Return format:
```

Every Luna response reports:

```text
Investigation:
Changes:
Commands:
Tests:
Risks:
Open issues:
```

The detailed role rules and examples live in `references/role-contracts.md`.

## Validation Strategy

Skill validation follows RED–GREEN–REFACTOR:

1. Run baseline pressure scenarios without the skill and record whether an
   agent stops after exploration, edits from multiple writers, skips diff
   review, overclaims a platform, or commits generated files.
2. Initialize the skill with the official skill scaffolder, implement the
   minimum guidance that closes observed failures, and run
   `quick_validate.py`.
3. Re-run equivalent scenarios with the skill and repository entry point.
4. Forward-test at least these cases in fresh agents:
   - “继续开发” from `current-stage.md`;
   - a small diagnosis that should not spawn Luna;
   - two independent read-only investigations that may run in parallel;
   - a high-risk architecture or concurrency request that must escalate;
   - a request to clean the external libwebrtc cache that must inspect first.
5. Inspect all outputs for role boundaries, stage completion, verification
   honesty, and unnecessary files.

Configuration validation proves the files, frontmatter, references, and Git
state. New-session discovery is verified in a fresh agent context. Automatic
hot reload in already-open external sessions is reported as runtime-dependent,
not claimed as verified.

## Delivery and Rollback

Deliver the workflow as one reviewed feature branch with focused commits for
the design and implementation. Verify the repository remains clean and the
baseline project tests still pass. Merge only after the workflow validation
and independent review gates pass.

Rollback is a normal Git revert of the workflow commits. Removing `AGENTS.md`,
`.agents/skills/shareme-sol-luna/`, and `docs/development/current-stage.md`
fully disables the repository-owned workflow without touching product code or
global Codex configuration.
