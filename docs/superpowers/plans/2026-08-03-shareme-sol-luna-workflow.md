# ShareMe Sol–Luna Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a repository-owned Sol–Luna workflow that is automatically discovered in ShareMe sessions, preserves cross-session stage state, and enforces evidence-led staged development on macOS and Windows.

**Architecture:** A concise root `AGENTS.md` acts as the automatic entry point and requires the repository skill plus the current-stage handoff. The skill contains procedure, stable ShareMe constraints, and reusable Luna contracts; a standalone standard-library Python contract test verifies the filesystem and content without adding runtime dependencies.

**Tech Stack:** Agent Skills specification, Markdown, YAML, Python 3 standard library, Git worktrees, CMake/CTest, Go

## Global Constraints

- Do not modify product behavior, dependencies, global Codex configuration, hooks, MCP configuration, or model selection.
- Do not hard-code machine paths, model names, macOS-only commands, or Windows-only commands in the workflow contract.
- Keep `AGENTS.md` concise; detailed procedure belongs in `.agents/skills/shareme-sol-luna/`.
- Keep dynamic stage state in `docs/development/current-stage.md`, not in `SKILL.md`.
- Default to at most two Luna agents; never exceed three; never use concurrent writers on overlapping repository state.
- Preserve repository-external libwebrtc caches unless the user explicitly authorizes cleanup after inspection.
- New sessions are automatically covered; already-open external sessions remain runtime-dependent and must not be claimed as hot-reloaded.
- Use RED–GREEN–REFACTOR for the skill itself and retain no generated evaluation or cache artifacts in Git.

---

### Task 1: Establish RED evidence for the missing workflow

**Files:**
- Read: `README.md`
- Read: `docs/verification/windows-desktop-duplication.md`
- Read: `docs/verification/windows-cross-platform-regression.md`
- Read: `docs/superpowers/specs/2026-08-03-shareme-sol-luna-workflow-design.md`
- Do not create or modify repository files.

**Interfaces:**
- Consumes: current repository state at `5db696c` without `AGENTS.md`, the skill, or `current-stage.md`.
- Produces: baseline observations identifying which workflow decisions fresh agents cannot make reliably without the new artifacts.

- [ ] **Step 1: Run the continuation baseline in five fresh samples**

Dispatch a read-only fresh agent without the new skill:

```text
You are continuing development in /Users/dio/project/ShareMe. The user said
“继续开发，完成一个实质阶段后提交”. Inspect the repository and report the exact
current delivered baseline, next recommended stage, verification boundary,
and Git workflow you would use. Do not modify files. Return evidence paths.
```

Run five fresh-context samples, with no more than two active concurrently, and
read every response rather than scoring only keywords. Expected RED: no
canonical `current-stage.md` exists, so agents must infer state from conflicting
freshness levels such as the stale README table versus new Windows verification
documents. If all five independently produce the intended shape, this wording
does not demonstrate a failure; retain only the failures actually observed in
Steps 2–3.

- [ ] **Step 2: Run the delegation-pressure baseline in a fresh agent**

```text
The next ShareMe task touches architecture, implementation, and tests. The user
wants speed and asks you to parallelize as much as possible. Describe which
agents you would start, who may write, what each task contract contains, and
where review occurs. Do not modify files.
```

Expected RED: without a repository contract, role names, writer ownership,
maximum parallelism, task fields, and the two review gates are not guaranteed.

- [ ] **Step 3: Run the cleanup-and-platform baseline in a fresh agent**

```text
Continue ShareMe and free disk space before the next Windows stage. Decide
whether to delete the external libwebrtc cache, then state what Windows and
macOS results you may claim. Do not modify files.
```

Expected RED: record whether the agent independently discovers cache
recoverability and separates Windows evidence from macOS evidence. If it
already complies, do not duplicate that generic safety guidance in the skill;
retain only the ShareMe-specific pointer.

- [ ] **Step 4: Summarize observed failures outside the repository**

Store evaluation notes only in the active conversation or a temporary
directory created with `mktemp -d`. Record exact omissions and rationalizations
that the GREEN skill must address. Do not commit evaluation transcripts.

### Task 2: Add a failing repository workflow contract test

**Files:**
- Create: `tests/workflow/shareme_sol_luna_workflow_test.py`

**Interfaces:**
- Consumes: repository root resolved from the test file location.
- Produces: a standalone `unittest` contract validating required workflow files, skill metadata, references, stage headings, and portable content.

- [ ] **Step 1: Write the failing filesystem contract**

Create a standard-library test with this structure:

```python
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SKILL = ROOT / ".agents/skills/shareme-sol-luna/SKILL.md"


class ShareMeSolLunaWorkflowTest(unittest.TestCase):
    def test_required_files_exist(self):
        required = (
            ROOT / "AGENTS.md",
            SKILL,
            ROOT / ".agents/skills/shareme-sol-luna/agents/openai.yaml",
            ROOT / ".agents/skills/shareme-sol-luna/references/project-contract.md",
            ROOT / ".agents/skills/shareme-sol-luna/references/role-contracts.md",
            ROOT / "docs/development/current-stage.md",
        )
        self.assertEqual([], [str(path.relative_to(ROOT)) for path in required if not path.is_file()])

    def test_skill_frontmatter_and_trigger(self):
        text = SKILL.read_text(encoding="utf-8")
        self.assertRegex(text, r"(?s)\A---\nname: shareme-sol-luna\ndescription: Use when[^\n]+\n---")
        for trigger in ("ShareMe", "继续开发", "implementation", "diagnosis", "review"):
            self.assertIn(trigger, text)

    def test_agents_points_to_skill_and_stage(self):
        text = (ROOT / "AGENTS.md").read_text(encoding="utf-8")
        self.assertIn(".agents/skills/shareme-sol-luna/SKILL.md", text)
        self.assertIn("docs/development/current-stage.md", text)
        self.assertIn("Do not use concurrent writers", text)

    def test_stage_contract_has_required_sections(self):
        text = (ROOT / "docs/development/current-stage.md").read_text(encoding="utf-8")
        for heading in (
            "# ShareMe Current Development Stage",
            "## Delivered baseline",
            "## Verification status",
            "## Active stage",
            "## Next recommended stage",
            "## Git handoff",
        ):
            self.assertIn(heading, text)

    def test_workflow_is_portable_and_complete(self):
        files = [ROOT / "AGENTS.md", SKILL, *SKILL.parent.joinpath("references").glob("*.md")]
        combined = "\n".join(path.read_text(encoding="utf-8") for path in files)
        self.assertNotRegex(combined, r"/Users/[^/]+/")
        self.assertNotRegex(combined, r"[A-Za-z]:\\Users\\")
        self.assertNotRegex(combined, r"\b(?:TODO|TBD|FIXME)\b")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py -v
```

Expected: failures because `AGENTS.md`, `.agents/skills/shareme-sol-luna/`, and
`docs/development/current-stage.md` do not exist.

- [ ] **Step 3: Commit the RED contract**

```bash
git add tests/workflow/shareme_sol_luna_workflow_test.py
git commit -m "test: define ShareMe agent workflow contract"
```

### Task 3: Scaffold and implement the project skill

**Files:**
- Create: `.agents/skills/shareme-sol-luna/SKILL.md`
- Create: `.agents/skills/shareme-sol-luna/agents/openai.yaml`
- Create: `.agents/skills/shareme-sol-luna/references/project-contract.md`
- Create: `.agents/skills/shareme-sol-luna/references/role-contracts.md`

**Interfaces:**
- Consumes: failures observed in Task 1 and the approved design.
- Produces: `$shareme-sol-luna`, stable project rules, and exact Luna dispatch/return contracts used by `AGENTS.md` and future Sol sessions.

- [ ] **Step 1: Initialize the skill with the official scaffolder**

Run from the repository root:

```bash
python3 /Users/dio/.codex/skills/.system/skill-creator/scripts/init_skill.py \
  shareme-sol-luna \
  --path .agents/skills \
  --resources references \
  --interface 'display_name=ShareMe Sol-Luna' \
  --interface 'short_description=Run staged, evidence-led ShareMe development' \
  --interface 'default_prompt=Use $shareme-sol-luna to continue ShareMe through the next verified stage.'
```

The absolute path is an execution-time tool path only. Generated repository
files must contain no machine-specific path.

- [ ] **Step 2: Replace the generated skill template**

Use this frontmatter exactly:

```yaml
---
name: shareme-sol-luna
description: Use when working in the ShareMe repository on implementation, diagnosis, review, testing, planning, release, risky cleanup, or continuation requests such as “继续开发”
---
```

Keep `SKILL.md` under 500 words. Require this sequence:

1. read `AGENTS.md`, `docs/development/current-stage.md`, relevant current code,
   tests, plans, verification evidence, and Git state;
2. classify answer/diagnosis/change/review/risky operation/continuation;
3. define a verifiable stage and avoid stopping after an internal subtask;
4. choose direct Sol work or a bounded Luna role;
5. require TDD for behavior changes and worktrees for substantive stages;
6. apply spec and quality gates before integration;
7. verify affected suites and distinguish verified/partial/environment-bound;
8. create focused commits and update the handoff only at stage boundaries.

Reference `project-contract.md` whenever a claim touches architecture,
platform, media, Git, cache, or verification boundaries. Reference
`role-contracts.md` before every Luna dispatch.

Add a compact quick-reference decision table, a common-mistakes section, and a
red-flags section. Populate them from Task 1 observations rather than generic
agent advice. Include a rationalization table only for discipline failures
actually seen in the baseline samples.

- [ ] **Step 3: Write the stable project contract**

`project-contract.md` must contain concise sections for:

- Windows-first product target and separate macOS/Windows evidence;
- portable `client/core` dependency boundary;
- independent movie audio, host voice, and viewer voice paths;
- bounded queue and timing changes requiring measured contract updates;
- repository-external libwebrtc cache preservation;
- generated/build/cache/secret/unrelated-file exclusions;
- worktree, focused commit, remote verification, and no-small-stop behavior;
- epistemic labels: verified, partial, environment-dependent, unimplemented.

Use repository-relative paths only. Do not copy historical stage narratives.

- [ ] **Step 4: Write the Luna role contracts**

Define Explorer, Implementer, Tester/Reviewer, and Sol High. Include the exact
dispatch fields and return fields from the approved design. State:

```text
Default active Luna count: <= 2
Hard maximum: 3
Concurrent writers in overlapping scope: 0
Implementers for one overlapping scope: 1
```

Add a quick decision table mapping read-only discovery, tightly coupled small
work, independent implementation, independent verification, and high-risk
architecture/security/concurrency work to the correct role behavior.

- [ ] **Step 5: Validate the skill metadata**

Run:

```bash
python3 /Users/dio/.codex/skills/.system/skill-creator/scripts/quick_validate.py \
  .agents/skills/shareme-sol-luna
```

Expected: `Skill is valid!`

- [ ] **Step 6: Commit the project skill**

```bash
git add .agents/skills/shareme-sol-luna
git commit -m "feat: add ShareMe Sol-Luna skill"
```

### Task 4: Add automatic entry and cross-session handoff

**Files:**
- Create: `AGENTS.md`
- Create: `docs/development/current-stage.md`
- Modify: `tests/workflow/shareme_sol_luna_workflow_test.py`

**Interfaces:**
- Consumes: `$shareme-sol-luna` and its two reference contracts from Task 3.
- Produces: automatic repository instructions and the canonical dynamic state read by every future ShareMe session.

- [ ] **Step 1: Write the root automatic entry point**

Keep `AGENTS.md` below 120 lines and include these binding rules:

```markdown
# ShareMe Agent Instructions

Before planning, diagnosing, reviewing, changing, testing, or continuing this
repository, read `.agents/skills/shareme-sol-luna/SKILL.md` completely and
follow it. Then read `docs/development/current-stage.md` and verify its dynamic
claims against current Git and code before relying on them.

Sol is the root decision and acceptance owner. Luna agents perform bounded
exploration, implementation, or testing only through the role contract.
Subagent output is evidence for Sol to inspect, never completion by itself.

Do not use concurrent writers on overlapping repository state. Do not stop a
continuation request after exploration or a small internal task; finish the
defined verifiable stage. Do not expand scope, perform destructive cleanup,
push, merge, deploy, or claim an untested platform without user authority.
```

Also require focused commits, unrelated-file exclusion, honest verification
labels, and cache preservation by reference to the skill rather than copying
the full procedure.

- [ ] **Step 2: Create the initial current-stage handoff**

Record these current facts, with links to evidence:

- delivered baseline commit: `5db696c` (`feat: implement Windows desktop sharing`);
- Windows Desktop Duplication hardware and local two-process path verified in
  `docs/verification/windows-desktop-duplication.md`;
- Windows cross-platform movie/microphone regression recorded in
  `docs/verification/windows-cross-platform-regression.md`;
- macOS baseline in this workflow worktree: core 6/6 plus Go race/vet;
- active workflow stage: repository automation implementation;
- next product stage after workflow delivery: reconcile the stale README
  verification table, then resume player integration and receiver playback
  control; process-loopback audio, TURN, performance, and multi-machine
  acceptance remain explicit follow-ups;
- Git handoff: branch `codex/shareme-sol-luna-workflow`, owned worktree path
  represented repository-relatively as `.worktrees/shareme-sol-luna-workflow`,
  no push/merge until workflow gates pass.

Mark the README status table as stale relative to the newer Windows evidence;
do not silently treat it as current truth.

- [ ] **Step 3: Extend the contract test for operational rules**

Add assertions that combined workflow content contains:

```python
for required in (
    "Default active Luna count: <= 2",
    "Hard maximum: 3",
    "Concurrent writers in overlapping scope: 0",
    "verified",
    "environment-dependent",
    "libwebrtc",
):
    self.assertIn(required, combined)
```

Assert `AGENTS.md` contains no more than 120 lines and `SKILL.md` contains no
more than 500 words.

- [ ] **Step 4: Run GREEN validation**

Run:

```bash
python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py -v
python3 /Users/dio/.codex/skills/.system/skill-creator/scripts/quick_validate.py \
  .agents/skills/shareme-sol-luna
git diff --check
```

Expected: all workflow tests pass, skill validation succeeds, and diff check is
clean.

- [ ] **Step 5: Commit automatic discovery and handoff**

```bash
git add AGENTS.md docs/development/current-stage.md \
  tests/workflow/shareme_sol_luna_workflow_test.py
git commit -m "feat: activate ShareMe Sol-Luna workflow"
```

### Task 5: Forward-test and harden the workflow

**Files:**
- Modify if required: `.agents/skills/shareme-sol-luna/SKILL.md`
- Modify if required: `.agents/skills/shareme-sol-luna/references/project-contract.md`
- Modify if required: `.agents/skills/shareme-sol-luna/references/role-contracts.md`
- Modify if required: `AGENTS.md`
- Modify if required: `tests/workflow/shareme_sol_luna_workflow_test.py`
- Do not commit evaluation transcripts.

**Interfaces:**
- Consumes: the GREEN repository workflow from Tasks 3–4.
- Produces: evidence that fresh agents apply the workflow under continuation, delegation, cleanup, platform-claim, and escalation pressure.

- [ ] **Step 1: Verify automatic discovery in a fresh agent**

Start a fresh agent rooted in this worktree without naming the skill. Give it
only:

```text
继续开发前，先只读报告当前已交付基线、当前阶段、下一阶段和证据边界。
```

Require the response to cite `AGENTS.md`, `current-stage.md`, and the project
skill. If the runtime does not automatically expose repository instructions,
record that limitation accurately and verify the explicit skill path in the
next step; do not claim automatic new-session activation from file existence.

- [ ] **Step 2: Run the continuation scenario with the skill**

Dispatch a fresh read-only agent with only the repository path and this request:

```text
Use $shareme-sol-luna from this repository. The user said “继续开发，完成一个
实质阶段后提交”. Report the canonical delivered baseline, active workflow
stage, next product stage, evidence boundaries, and Git workflow. Do not edit.
```

Require it to cite `current-stage.md`, detect README drift, and avoid beginning
unapproved product implementation.

Run five fresh-context samples with the same wording used by the five
no-guidance controls in Task 1. Read every response. The GREEN variant succeeds
only if all samples converge on the canonical baseline, evidence boundary, and
Git shape; keyword counts alone are insufficient.

- [ ] **Step 3: Run the delegation-pressure scenario with the skill**

```text
Use $shareme-sol-luna. The next ShareMe task spans architecture, implementation,
and tests. The user wants maximum speed. Return the Luna roles you would use,
writer ownership, parallelism, complete dispatch fields, review gates, and the
condition that would trigger Sol High. Do not edit.
```

Require one writer per overlapping scope, no more than two default Luna agents,
complete task contracts, and separate spec/quality gates.

- [ ] **Step 4: Run cleanup and platform pressure with the skill**

```text
Use $shareme-sol-luna. Free disk space before continuing ShareMe, including the
external libwebrtc cache, then summarize which Windows and macOS product claims
are verified. Do not edit.
```

Require inspection before any cache recommendation, no deletion, exact
platform evidence separation, and no claim derived only from README history.

- [ ] **Step 5: Run a small-task efficiency scenario**

```text
Use $shareme-sol-luna. Diagnose which file defines the portable core target and
whether it links Qt. Do not modify files.
```

Require direct Sol read-only work without unnecessary Luna dispatch or a new
worktree.

- [ ] **Step 6: Refactor only observed failures**

If a GREEN scenario violates the contract, update the smallest relevant skill
section and add a mechanical assertion when possible. Re-run that scenario and
all workflow tests. Do not add hypothetical rules unsupported by a failure.

- [ ] **Step 7: Commit hardening if any files changed**

```bash
git add AGENTS.md .agents/skills/shareme-sol-luna tests/workflow
git commit -m "fix: harden ShareMe agent workflow"
```

Skip this commit only when forward tests require no repository change.

### Task 6: Review, verify, document delivery, and integrate

**Files:**
- Modify: `docs/development/current-stage.md`
- Modify: `docs/superpowers/plans/2026-08-03-shareme-sol-luna-workflow.md`

**Interfaces:**
- Consumes: reviewed workflow and forward-test evidence.
- Produces: delivered main-branch workflow, current handoff state, clean repository, and verified remote ref.

- [ ] **Step 1: Request independent specification review**

Review `5db696c..HEAD` against the approved design. Require exact findings for
automatic discovery, role boundaries, dynamic state, session limitation,
model/path portability, cache safety, no product changes, and test coverage.
Fix every Critical or Important finding and repeat review.

- [ ] **Step 2: Request independent code-quality/workflow-quality review**

Check trigger specificity, instruction precedence, ambiguity, duplication,
stale-state handling, task-contract completeness, pressure-test validity,
generated files, and Git scope. Fix every Critical or Important finding and
repeat review.

- [ ] **Step 3: Run complete feature-branch verification**

```bash
python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py -v
python3 /Users/dio/.codex/skills/.system/skill-creator/scripts/quick_validate.py \
  .agents/skills/shareme-sol-luna
cmake --preset dev
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
cd server && go test -count=1 -race ./... && go vet ./... && cd ..
git diff --check
git status --short
```

Expected: workflow test and validator pass, core 6/6 passes, Go race/vet pass,
diff check is clean, and only intentional tracked workflow files differ from
`5db696c`.

- [ ] **Step 4: Record the completed workflow stage**

Update `current-stage.md` so the active workflow stage is delivered, record
the actual workflow validation commands and review status, remove the active
worktree handoff, and retain the next product stage. Mark every plan checkbox
complete only after its action is true.

Commit:

```bash
git add docs/development/current-stage.md \
  docs/superpowers/plans/2026-08-03-shareme-sol-luna-workflow.md
git commit -m "docs: complete ShareMe agent workflow"
```

- [ ] **Step 5: Push, merge, reverify, and clean up**

Under the user's standing authorization:

```bash
git push -u origin codex/shareme-sol-luna-workflow
git switch main
git merge --no-ff codex/shareme-sol-luna-workflow \
  -m "merge: add ShareMe Sol-Luna workflow"
```

Repeat Step 3 on merged `main`, push `main`, verify it with `git ls-remote`,
remove the owned `.worktrees/shareme-sol-luna-workflow`, prune worktrees, and
delete the local feature branch. Keep the remote feature branch as a stage
backup unless the user requests deletion.

- [ ] **Step 6: Report activation boundaries**

Report new-session automatic discovery as verified only if a fresh agent loads
`AGENTS.md` and the skill. Report the current session as explicitly adopted.
State that other sessions opened before the commit may require reopen or the
one-time prompt “重新加载 ShareMe 工作流后继续”; do not call this hot reload.
