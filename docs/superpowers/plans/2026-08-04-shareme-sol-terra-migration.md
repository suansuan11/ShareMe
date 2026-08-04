# ShareMe Sol–Terra Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace every active ShareMe Sol–Luna workflow entry point with the verified Sol–Terra route.

**Architecture:** Keep Sol/medium as the project root. Rename the active skill and role entry points to Terra, pin Terra/medium for the two bounded child roles, retain the two-child and one-writer invariants, and preserve historical Luna plans/specifications unchanged.

**Tech Stack:** Codex TOML, Markdown, Python standard-library `unittest`, filesystem rename operations.

## Global Constraints

- Do not change product source, global Codex configuration, dependency caches, or historical plans/specifications.
- Back up `.codex/config.toml` and both active agent files to a timestamped directory under `/private/tmp` before changing them.
- Keep `gpt-5.6-sol` and medium reasoning for the root; pin `gpt-5.6-terra` and medium reasoning for both roles.
- Retain at most two independent read-only child tasks and exactly one writer per implementation scope.
- Do not use Luna or a fallback route in active configuration, policy, validation, or dynamic handoff.
- Do not push, deploy, or modify existing user work outside this migration.

---

### Task 1: Define the active Sol–Terra contract

**Files:**

- Rename: `tests/workflow/shareme_sol_luna_workflow_test.py` to `tests/workflow/shareme_sol_terra_workflow_test.py`

**Interfaces:**

- Consumes: active project config, named agent files, root instructions, skill package, validator, and stage handoff.
- Produces: static assertions requiring `shareme-sol-terra`, `terra_explorer`, `terra_implementer`, and `gpt-5.6-terra`.

- [x] **Step 1: Rename and rewrite the contract test**

Rename the test file and class to `ShareMeSolTerraWorkflowTest`. Change its
active paths, mutation fixtures, model assertions, and required policy tokens:

```python
SKILL = ROOT / ".agents/skills/shareme-sol-terra/SKILL.md"
TERRA_EXPLORER = ROOT / ".codex/agents/terra_explorer.toml"
TERRA_IMPLEMENTER = ROOT / ".codex/agents/terra_implementer.toml"
class ShareMeSolTerraWorkflowTest(unittest.TestCase):
    def test_runtime_configuration_is_deterministic_and_bounded(self):
        config = CONFIG.read_text(encoding="utf-8")
        self.assertIn('default_subagent_model = "gpt-5.6-terra"', config)
        for name, agent_path in (
            ("terra_explorer", TERRA_EXPLORER),
            ("terra_implementer", TERRA_IMPLEMENTER),
        ):
            self.assertIn(f'name = "{name}"', agent_path.read_text(encoding="utf-8"))
```

Require `at most two independent Terra`, `independent read-only`, and
`one writer`. Preserve the existing cross-platform and cache guardrail
assertions.

- [x] **Step 2: Run the focused RED test**

Run:

```sh
python3 -m unittest tests.workflow.shareme_sol_terra_workflow_test -v
```

Expected: failure because active paths and model strings still reference Luna.

- [x] **Step 3: Commit the RED contract**

```sh
git add tests/workflow/shareme_sol_terra_workflow_test.py
git rm tests/workflow/shareme_sol_luna_workflow_test.py
git commit -m "test: define ShareMe Sol-Terra workflow"
```

### Task 2: Migrate the active configuration and entry points

**Files:**

- Modify: `.codex/config.toml`
- Rename: `.codex/agents/luna_explorer.toml` to `.codex/agents/terra_explorer.toml`
- Rename: `.codex/agents/luna_implementer.toml` to `.codex/agents/terra_implementer.toml`
- Modify: `AGENTS.md`
- Rename: `.agents/skills/shareme-sol-luna/` to `.agents/skills/shareme-sol-terra/`
- Modify: `.agents/skills/shareme-sol-terra/SKILL.md`
- Modify: `.agents/skills/shareme-sol-terra/agents/openai.yaml`
- Modify: `.agents/skills/shareme-sol-terra/references/role-contracts.md`
- Modify: `scripts/validate_shareme_skill.py`

**Interfaces:**

- Consumes: Task 1's failing Terra contract.
- Produces: deterministic project configuration and named roles that the Sol runtime can dispatch without a model fallback.

- [x] **Step 1: Create the required reversible backup**

```sh
backup_root="/private/tmp/shareme-sol-terra-config-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$backup_root/agents"
cp .codex/config.toml "$backup_root/config.toml"
cp .codex/agents/luna_explorer.toml "$backup_root/agents/luna_explorer.toml"
cp .codex/agents/luna_implementer.toml "$backup_root/agents/luna_implementer.toml"
print "$backup_root"
```

- [x] **Step 2: Rename active role and skill paths**

Use explicit `mv` commands to rename only the two role files and the
`shareme-sol-luna` skill directory to their Terra names. Do not rename any
file beneath `docs/superpowers/plans` or `docs/superpowers/specs`.

- [x] **Step 3: Apply the Terra model and policy changes**

Set the project default:

```toml
default_subagent_model = "gpt-5.6-terra"
default_subagent_reasoning_effort = "medium"
```

Set both role files to `name = "terra_explorer"` or
`name = "terra_implementer"` and `model = "gpt-5.6-terra"`. Preserve the
explorer's `sandbox_mode = "read-only"`; preserve the implementer's bounded
writer restrictions. Change active skill metadata, prompt, validator
`DEFAULT_SKILL_DIR`, `EXPECTED_INTERFACE`, root `AGENTS.md`, and role
contract terminology to Sol–Terra while retaining the two-independent-read-only
and one-writer limits.

- [x] **Step 4: Run GREEN static checks**

```sh
python3 -m unittest tests/workflow/shareme_sol_terra_workflow_test.py -v
python3 scripts/validate_shareme_skill.py
git diff --check
```

Expected: all tests pass, validator prints `Skill is valid!`, and the diff
check prints no output.

- [x] **Step 5: Commit the migrated active workflow**

```sh
git add AGENTS.md .codex .agents/skills/shareme-sol-terra scripts/validate_shareme_skill.py tests/workflow
git add -u .agents/skills .codex tests/workflow
git commit -m "feat: migrate ShareMe workflow to Sol-Terra"
```

### Task 3: Record the verified route and perform an actual dispatch

**Files:**

- Modify: `docs/development/current-stage.md`
- Modify: `docs/superpowers/plans/2026-08-04-shareme-sol-terra-migration.md`

**Interfaces:**

- Consumes: passing static Terra contract and active role configuration.
- Produces: a dynamic handoff that distinguishes static configuration from a fresh Desktop Sol-to-Terra dispatch result.

- [x] **Step 1: Update dynamic handoff**

Replace active Luna workflow references with Sol–Terra. Record that a fresh
Sol/medium ShareMe session must dispatch `terra_explorer` without fallback.
Keep historical Luna plans/specifications unmodified and do not claim exact
runtime model identifiers if Codex does not expose them.

- [x] **Step 2: Verify the full repository checks**

```sh
python3 -m unittest tests/workflow/shareme_sol_terra_workflow_test.py -v
python3 scripts/validate_shareme_skill.py
cmake --preset dev
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
(cd server && go test -count=1 -race ./... && go vet ./...)
git diff --check
```

- [x] **Step 3: Run a fresh Desktop Sol-to-Terra probe**

Create a new local ShareMe Codex task with the project's default root model.
Instruct it to use `terra_explorer` for a read-only
`client/rtc/desktop` directory analysis. Verify the spawned agent reports
files, symbols, and call relationships; record the child creation identifier,
configured Terra model, absence of fallback, and any unobservable exact
runtime identifiers.

- [x] **Step 4: Complete the plan and commit the handoff**

Mark every completed checkbox. Then run:

```sh
git add docs/development/current-stage.md docs/superpowers/plans/2026-08-04-shareme-sol-terra-migration.md
git commit -m "docs: verify ShareMe Sol-Terra routing"
```
