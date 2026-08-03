# ShareMe Dynamic Luna Model Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make ShareMe's Sol-Luna workflow select an explicit, lower-cost adequate Luna model at runtime, audit every selection and fallback, and distinguish routed quality evidence from unmeasured realized credit savings.

**Architecture:** Stable tracked policy describes capability tiers and required audit fields without model slugs or prices. Sol resolves the current runtime's available models at each dispatch, records the exact requested model only in the live task contract, and retains architecture and acceptance authority. A standard-library contract test locks the durable rules, while a verification document records the dated A/B evidence and its measurement boundary.

**Tech Stack:** Agent Skills Markdown, Python 3 `unittest`, Git worktrees, Codex subagent runtime

## Global Constraints

- Do not add `.codex/config.toml`, change global Codex configuration, or pin model slugs or pricing values in stable workflow policy.
- Keep `SKILL.md` at no more than 500 words and use repository-relative paths in tracked workflow files.
- Always specify a model and reasoning effort explicitly for Luna when the runtime supports overrides; otherwise record the fallback and make no savings claim.
- Prefer direct Sol work when delegation coordination would cost more than it saves; never duplicate agents merely to reduce model cost.
- Keep ambiguous architecture, security, deletion, concurrency, lifetime, consistency, integration, and final acceptance with Sol.
- Preserve every existing role, filesystem, Git, cache, platform, review, and verification boundary.
- Do not change product code or dependencies in this plan.
- Keep generated A/B transcripts outside Git and commit only focused workflow, test, verification, and handoff files.

---

### Task 1: Lock the missing routing contract with RED evidence

**Files:**
- Modify: `tests/workflow/shareme_sol_luna_workflow_test.py`

**Interfaces:**
- Consumes: the combined content of `AGENTS.md`, `SKILL.md`, `current-stage.md`, and skill references.
- Produces: contract assertions that fail until stable dynamic routing and honest credit-reporting rules exist.

- [x] **Step 1: Add required routing phrases**

Extend `test_workflow_is_portable_and_complete` with these exact required
strings:

```python
"Inspect the model choices exposed by the current runtime"
"Target capability tier:"
"Requested model:"
"Requested reasoning effort:"
"Selection reason:"
"Fallback or difference:"
"Never claim measured credit savings without per-agent usage telemetry"
"Never create duplicate agents merely to save credits"
```

Also reject stable model slugs and tracked project Codex configuration:

```python
self.assertNotRegex(combined, r"gpt-[0-9]")
self.assertFalse((ROOT / ".codex/config.toml").exists())
```

- [x] **Step 2: Run the focused test and verify RED**

Run:

```bash
python3 -m unittest \
  tests.workflow.shareme_sol_luna_workflow_test.ShareMeSolLunaWorkflowTest.test_workflow_is_portable_and_complete
```

Expected: failure on the first missing routing phrase, proving inherited-model
behavior is not yet protected by the repository contract.

- [x] **Step 3: Commit the RED contract**

```bash
git add tests/workflow/shareme_sol_luna_workflow_test.py
git commit -m "test: define dynamic Luna routing contract"
```

### Task 2: Implement stable dynamic selection and audit rules

**Files:**
- Modify: `.agents/skills/shareme-sol-luna/SKILL.md`
- Modify: `.agents/skills/shareme-sol-luna/references/role-contracts.md`
- Test: `tests/workflow/shareme_sol_luna_workflow_test.py`

**Interfaces:**
- Consumes: Task 1's exact routing assertions and the approved design.
- Produces: a model-agnostic runtime selection procedure plus mandatory live dispatch metadata.

- [x] **Step 1: Add the skill-level routing step**

Before every Luna dispatch, require Sol to inspect the model choices exposed by
the current runtime, select the least expensive adequate capability tier, and
read the role contract. State that delegation is skipped when coordination
cost exceeds its benefit. Refactor existing wording so `SKILL.md` remains at
or below 500 words.

- [x] **Step 2: Add the role-contract routing section**

Add a `Dynamic model routing` section that requires:

```text
Target capability tier:
Requested model:
Requested reasoning effort:
Selection reason:
Cost-tier basis:
Fallback or difference:
```

Define low-cost routing for bounded exploration, routine tests, and narrowly
specified implementation; keep high-risk judgment and final acceptance with
Sol. Require an explicit model and reasoning value when supported. If an
override is unavailable or rejected, inherit the parent, record the fallback,
and make no savings claim. Include the exact sentences guarded by Task 1.

- [x] **Step 3: Run GREEN validation**

```bash
python3 -m unittest tests.workflow.shareme_sol_luna_workflow_test -v
python3 scripts/validate_shareme_skill.py
wc -w .agents/skills/shareme-sol-luna/SKILL.md
git diff --check
```

Expected: 7/7 workflow tests pass, `Skill is valid!`, word count is at most
500, and the diff check is clean.

- [x] **Step 4: Commit the stable routing policy**

```bash
git add .agents/skills/shareme-sol-luna/SKILL.md \
  .agents/skills/shareme-sol-luna/references/role-contracts.md
git commit -m "feat: route Luna work by runtime capability"
```

### Task 3: Verify routed quality and record the delivered boundary

**Files:**
- Create: `docs/verification/shareme-dynamic-luna-routing.md`
- Modify: `docs/development/current-stage.md`
- Modify: `docs/superpowers/plans/2026-08-03-shareme-dynamic-model-routing.md`

**Interfaces:**
- Consumes: the stable policy from Task 2 and a fresh runtime with explicit model override support.
- Produces: dated routing/quality evidence, an honest credit-measurement label, and a future-session handoff.

- [x] **Step 1: Dispatch one fresh low-cost Luna Explorer**

Use the complete role contract and explicitly select the current lower-cost
adequate model plus balanced reasoning. The task must be filesystem-read-only:
verify `HEAD`, `origin/main`, Windows evidence, stale README statements, and
the canonical next stage. Save the full prompt and response only in this
plan's ignored SDD workspace.

- [x] **Step 2: Independently verify its output**

Sol checks every SHA, path, test count, next-stage statement, and Git-visible
repository change claim against current Git and tracked files. Record routing
as verified only if the runtime accepted the override and all acceptance facts
match. Record realized credit saving as unmeasured because per-agent
token/credit telemetry is unavailable.

- [x] **Step 3: Write the verification document**

Record the dated environment, target/actual requested model, reasoning effort,
complete task boundary, comparison result, exact repository facts,
Git-visible repository status, and limitations. Historical verification may
name the actual model; stable skill and role policy must remain model-agnostic.

- [x] **Step 4: Update the current-stage handoff and plan**

Mark dynamic Luna routing delivered on the feature branch, link the verification
document, retain README reconciliation as the immediate next stage, and check
only plan actions already completed.

- [x] **Step 5: Commit verification and handoff**

```bash
git add docs/verification/shareme-dynamic-luna-routing.md \
  docs/development/current-stage.md \
  docs/superpowers/plans/2026-08-03-shareme-dynamic-model-routing.md
git commit -m "docs: verify dynamic Luna model routing"
```

### Task 4: Review, integrate, and transition to product work

**Files:**
- Modify after integration: `docs/development/current-stage.md`
- Modify after integration: `docs/superpowers/plans/2026-08-03-shareme-dynamic-model-routing.md`

**Interfaces:**
- Consumes: Tasks 1-3 commits and verification evidence.
- Produces: reviewed and remotely verified `main`, cleaned local feature state, and an accurate next product stage.

- [x] **Step 1: Run independent specification review**

Require a filesystem-read-only reviewer to compare the complete branch against
the approved design, including portability, routing precedence, fallback,
credit-claim boundaries, unchanged authority, and test coverage. Fix all
Critical and Important findings and repeat review.

- [x] **Step 2: Run independent quality review**

Require a separate filesystem-read-only reviewer to inspect clarity,
duplication, trigger behavior, word count, test brittleness, evidence honesty,
and Git scope. Fix all Critical and Important findings and repeat review.

- [x] **Step 3: Run final branch verification**

```bash
python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test -v
python3 scripts/validate_shareme_skill.py
cmake --preset dev
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
cd server && go test -count=1 -race ./... && go vet ./... && cd ..
git diff --check
git status --short --branch
```

Expected: workflow 7/7, valid skill, portable C++ 6/6, Go race/vet success,
clean diff check, and only intentional commits relative to `main`.

- [ ] **Step 4: Push, merge, reverify, and clean up**

Push the feature branch and verify its remote SHA. Merge into current `main`
with `--no-ff`, repeat Step 3, push and verify `origin/main`, remove only this
plan's owned worktree and local feature branch, and retain the remote feature
branch as a stage backup.

- [ ] **Step 5: Record integration completion**

On `main`, mark the remaining plan checkboxes complete, record the merge and
remote state in `current-stage.md`, commit with
`docs: record dynamic Luna routing integration`, rerun the focused workflow
checks, and push the exact final `main` SHA.
