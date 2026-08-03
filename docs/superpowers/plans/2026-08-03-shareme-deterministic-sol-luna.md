# ShareMe Deterministic Sol-Luna Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make new ShareMe tasks use the project Sol default and a single Luna worker default, while retaining only essential safety and verification rules.

**Architecture:** `.codex/config.toml` supplies the main model and bounded-agent defaults. Two project custom-agent TOML files define a read-only explorer and a single frozen-scope implementer. A Python standard-library contract test verifies these runtime artifacts and the concise policy.

**Tech Stack:** Codex TOML configuration, Python `unittest` and `tomllib`, Markdown.

## Global Constraints

- Repository scope only: never edit `~/.codex/config.toml`.
- Preserve platform-evidence, cache-preservation, focused-commit, and single-writer rules.
- New trusted ShareMe tasks load the configuration; an existing task does not change model.
- Never claim realized credit saving without comparable usage telemetry.
- Do not change product code, dependencies, generated output, or external caches.

---

### Task 1: Define the deterministic runtime contract with a failing test

**Files:**

- Modify: `tests/workflow/shareme_sol_luna_workflow_test.py`

**Interfaces:**

- Consumes: `ROOT` and the repository's standard-library Python runtime.
- Produces: `test_runtime_configuration_is_deterministic_and_bounded`.

- [x] **Step 1: Write the failing test**

Add constants for `.codex/config.toml` and both agent files, then assert their
required TOML key/value text: Sol/medium, enabled one-child Luna/medium defaults,
both role identities, and their boundaries. The installed Codex parser remains
the TOML syntax authority because this macOS Python 3.9 runtime lacks `tomllib`.

- [x] **Step 2: Run the test to verify RED**

Run `python3 -m unittest tests.workflow.shareme_sol_luna_workflow_test.ShareMeSolLunaWorkflowTest.test_runtime_configuration_is_deterministic_and_bounded -v`.

Expected: failure because the required `.codex` files do not exist.

- [x] **Step 3: Commit the RED contract**

Run `git add tests/workflow/shareme_sol_luna_workflow_test.py` and `git commit -m "test: define deterministic ShareMe agent configuration"`.

### Task 2: Add the project configuration and minimal custom agents

**Files:**

- Create: `.codex/config.toml`
- Create: `.codex/agents/luna_explorer.toml`
- Create: `.codex/agents/luna_implementer.toml`

**Interfaces:**

- Consumes: the failing runtime contract.
- Produces: deterministic project defaults and two named Luna agents.

- [x] **Step 1: Add project configuration**

Create the following file:

```toml
model = "gpt-5.6-sol"
model_reasoning_effort = "medium"

[agents]
enabled = true
max_concurrent_threads_per_session = 1
default_subagent_model = "gpt-5.6-luna"
default_subagent_reasoning_effort = "medium"
```

- [x] **Step 2: Add custom-agent files**

The explorer is read-only and only returns requested paths, symbols, commands,
and evidence; it may not edit, commit, change dependencies, or prescribe an
unrequested redesign. The implementer may edit only its frozen target and run
named tests; it may not commit, expand scope, clean unrelated files, or alter
caches. Both files pin Luna/medium and define `name`, `description`, and
`developer_instructions`.

- [x] **Step 3: Run GREEN and parse validation**

Run the focused test from Task 1, then invoke the bundled desktop Codex with
`app-server --listen off`; the expected no-transport result must be reached
without a project-configuration error. Strict validation is
environment-dependent when an unrelated user-level configuration field blocks
it; do not modify that global configuration in this repository stage.

- [x] **Step 4: Commit configuration**

Run `git add .codex/config.toml .codex/agents/luna_explorer.toml .codex/agents/luna_implementer.toml` and `git commit -m "feat: configure deterministic ShareMe agent roles"`.

### Task 3: Replace the costly workflow policy with direct-first rules

**Files:**

- Modify: `AGENTS.md`
- Modify: `.agents/skills/shareme-sol-luna/SKILL.md`
- Modify: `.agents/skills/shareme-sol-luna/references/role-contracts.md`
- Modify: `tests/workflow/shareme_sol_luna_workflow_test.py`

**Interfaces:**

- Consumes: the two role names and runtime defaults.
- Produces: a concise direct-first policy.

- [x] **Step 1: Write the failing policy test**

Replace dynamic-routing fields, 15-field dispatch templates, fallback-return
requirements, and the assertion that config is absent. Require the policy to
contain `Sol works directly by default`, `at most one active Luna`, both role
names, `one writer`, and `Unmeasured` credit saving. Limit `SKILL.md` to 300
words and role contracts to 350 words.

- [x] **Step 2: Run the test to verify RED**

Run `python3 -m unittest tests.workflow.shareme_sol_luna_workflow_test.ShareMeSolLunaWorkflowTest.test_workflow_is_portable_and_complete -v`.

Expected: failure because old instructions still require dynamic routing and
verbose templates.

- [x] **Step 3: Write concise instructions**

Keep AGENTS as an entry point. Keep in the skill only the project-contract
trigger, verifiable stage, direct-first delegation, one writer, focused
verification, and evidence labels. The role contract defines the two agents and
a short request with target, scope, acceptance, and return summary. Remove
price/model/fallback records, mandatory double review, and Sol High role.

- [x] **Step 4: Run focused checks and commit**

Run `python3 -m unittest tests.workflow.shareme_sol_luna_workflow_test -v`,
`python3 scripts/validate_shareme_skill.py`, and `git diff --check`, then run
`git add AGENTS.md .agents/skills/shareme-sol-luna tests/workflow/shareme_sol_luna_workflow_test.py` and `git commit -m "refactor: simplify ShareMe agent workflow"`.

### Task 4: Record the migration boundary and verify the stage

**Files:**

- Modify: `docs/development/current-stage.md`
- Modify: `docs/verification/shareme-dynamic-luna-routing.md`
- Modify: `docs/superpowers/specs/2026-08-03-shareme-dynamic-model-routing-design.md`
- Modify: `docs/superpowers/plans/2026-08-03-shareme-deterministic-sol-luna.md`

**Interfaces:**

- Consumes: passing config and policy verification.
- Produces: an honest stage handoff.

- [x] **Step 1: Mark prior routing evidence superseded**

Add a historical notice to the prior design and verification. Retain the dated
explicit-override observation but state it did not configure defaults, prove
lasting savings, or govern future ShareMe sessions.

- [x] **Step 2: Update current-stage handoff**

Record the deterministic configuration stage, commit, tests, strict-parser
result, and boundaries: new-session activation is environment-dependent unless
observed; credit savings are unmeasured; no product or platform evidence changed.

- [x] **Step 3: Run complete verification**

Run `python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py -v`,
`python3 scripts/validate_shareme_skill.py`, `cmake --preset dev`,
`cmake --build --preset build-dev`, `ctest --preset test-dev --output-on-failure`,
`(cd server && go test -count=1 -race ./... && go vet ./...)`,
`git diff --check`, and `git status --short --branch`.

Expected: record exact results; workflow/parser evidence is verified only when
commands pass, while runtime activation and credit savings retain their stated
boundaries.

- [x] **Step 4: Complete plan and commit handoff**

Mark completed boxes, then add the three documentation files and this plan and
commit with `docs: record deterministic ShareMe agent workflow`.
