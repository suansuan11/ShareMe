# ShareMe Luna Parallelism Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retain Luna configuration while permitting up to two independent Luna subagents in a trusted ShareMe task.

**Architecture:** The project agent cap changes from one to two; named Luna role files remain pinned to Luna/medium. Concise policy text permits only independent read-only work in parallel and preserves a single writer for every implementation scope.

**Tech Stack:** Codex TOML configuration, Python standard-library `unittest`, Markdown.

## Global Constraints

- Do not modify global Codex configuration or substitute Terra for Luna.
- Keep Sol direct-first and preserve one writer for an overlapping implementation scope.
- Do not alter product code, the movie-audio worktree, dependency caches, push, merge, or deployment.
- Mark Luna execution environment-dependent until the current runtime accepts it.

---

### Task 1: Define the two-Luna contract with a failing test

**Files:**

- Modify: `tests/workflow/shareme_sol_luna_workflow_test.py`

**Interfaces:**

- Consumes: the project config, skill, and role contract.
- Produces: static assertions for a two-agent cap, independent read-only parallelism, and a single writer.

- [x] **Step 1: Write the failing test**

Replace all `max_concurrent_threads_per_session = 1` and `at most one active Luna`
expectations with `2` and `at most two independent Luna`. Assert the role
contract includes `independent read-only` and `one writer`, and still contains
the Luna/medium configuration assertions.

- [x] **Step 2: Run RED**

Run `python3 -m unittest tests.workflow.shareme_sol_luna_workflow_test.ShareMeSolLunaWorkflowTest.test_runtime_configuration_is_deterministic_and_bounded tests.workflow.shareme_sol_luna_workflow_test.ShareMeSolLunaWorkflowTest.test_workflow_is_portable_and_complete tests.workflow.shareme_sol_luna_workflow_test.ShareMeSolLunaWorkflowTest.test_direct_first_role_contract_is_concise -v`.

Expected: failure because the current configuration and policy cap active Luna
agents at one.

- [x] **Step 3: Commit the RED test**

Run `git add tests/workflow/shareme_sol_luna_workflow_test.py` and `git commit -m "test: define ShareMe Luna parallelism"`.

### Task 2: Implement bounded independent parallelism

**Files:**

- Modify: `.codex/config.toml`
- Modify: `.agents/skills/shareme-sol-luna/SKILL.md`
- Modify: `.agents/skills/shareme-sol-luna/references/role-contracts.md`

**Interfaces:**

- Consumes: Task 1's two-Luna contract.
- Produces: two allowed active Luna threads, unchanged Luna model pins, and a clear concurrent-write prohibition.

- [x] **Step 1: Change only the concurrency cap**

Set `max_concurrent_threads_per_session = 2`. Keep all Sol and Luna model and
reasoning values unchanged.

- [x] **Step 2: Update direct-first policy**

Replace the one-active-Luna wording with `at most two independent Luna` in the
skill and role contract. Permit two Explorers, or independent read-only test or
log analysis, in parallel. State that an Implementer is the only writer and
that dependent review/test work waits for its write phase.

- [x] **Step 3: Run GREEN checks**

Run `python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py -v`,
`python3 scripts/validate_shareme_skill.py`, and `git diff --check`.

Expected: all workflow tests pass, the validator reports `Skill is valid!`, and
the diff check has no output.

- [x] **Step 4: Commit the policy**

Run `git add .codex/config.toml .agents/skills/shareme-sol-luna tests/workflow/shareme_sol_luna_workflow_test.py` and `git commit -m "feat: allow independent ShareMe Luna parallelism"`.

### Task 3: Record verification limits and complete the stage

**Files:**

- Modify: `docs/development/current-stage.md`
- Modify: `docs/superpowers/plans/2026-08-03-shareme-luna-parallelism.md`

**Interfaces:**

- Consumes: passing static contract and policy checks.
- Produces: a handoff that distinguishes configured two-Luna capacity from unobserved Luna runtime availability.

- [x] **Step 1: Update handoff evidence**

Record the two-Luna independent-work cap, one-writer invariant, unchanged Luna
model configuration, and runtime limitation: a current task that rejects Luna
is environment-dependent and must not be replaced by Terra or treated as a
configuration pass.

- [x] **Step 2: Run full repository verification**

Run `python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py -v`,
`python3 scripts/validate_shareme_skill.py`, `cmake --preset dev`,
`cmake --build --preset build-dev`, `ctest --preset test-dev --output-on-failure`,
`(cd server && go test -count=1 -race ./... && go vet ./...)`,
`git diff --check`, and `git status --short --branch`.

Expected: record exact macOS results and preserve the Luna runtime check as
environment-dependent unless a fresh task accepts it.

- [x] **Step 3: Complete the plan and commit handoff**

Mark completed boxes, then run `git add docs/development/current-stage.md docs/superpowers/plans/2026-08-03-shareme-luna-parallelism.md` and `git commit -m "docs: record ShareMe Luna parallelism"`.
