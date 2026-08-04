# ShareMe Root Model Choice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let newly created ShareMe tasks select an available root model without a project-level Sol pin, while preserving Terra child-agent routing.

**Architecture:** Remove the two root-model keys from `.codex/config.toml`. Update the workflow regression test to reject a root pin while retaining all Terra child-route assertions. Update the dynamic handoff to distinguish client-controlled root selection from project-configured Terra child roles.

**Tech Stack:** TOML, Python `unittest`, repository skill validator.

## Global Constraints

- Remove only project-level root `model` and `model_reasoning_effort` settings.
- Retain Terra as the medium-reasoning child route and retain each role's access boundary.
- Do not claim a particular root model is available until a fresh Codex session demonstrates it.

---

### Task 1: Remove the root-model pin and prove the routing contract

**Files:**
- Modify: `.codex/config.toml:1-8`
- Modify: `tests/workflow/shareme_sol_terra_workflow_test.py:18-37`
- Modify: `docs/development/current-stage.md:10-13,58-63`

**Interfaces:**
- Consumes: TOML root keys and `[agents]` child-routing keys.
- Produces: no project root-model pin, with a regression test requiring the Terra child defaults.

- [ ] **Step 1: Write the failing test**

Replace the root-model requirements with these assertions, while retaining the existing `[agents]`, thread-limit, and Terra/medium assertions:

```python
self.assertNotIn('model = "gpt-5.6-sol"', config)
self.assertNotIn('model_reasoning_effort = "medium"', config)
```

- [ ] **Step 2: Run the focused test to verify it fails**

Run `python3 -m unittest tests/workflow/shareme_sol_terra_workflow_test.py -v`.

Expected: `test_runtime_configuration_is_deterministic_and_bounded` fails because the current configuration pins the root model to Sol.

- [ ] **Step 3: Implement the minimal configuration and handoff update**

Delete these two top-level TOML entries only:

```toml
model = "gpt-5.6-sol"
model_reasoning_effort = "medium"
```

State in the handoff that root-model selection belongs to Codex client/user configuration, while the project configures Terra child roles at medium reasoning effort.

- [ ] **Step 4: Run acceptance checks**

Run `python3 -m unittest tests/workflow/shareme_sol_terra_workflow_test.py -v`, `python3 scripts/validate_shareme_skill.py`, and `git diff --check`.

Expected: all eight workflow tests pass, the validator prints `Skill is valid!`, and `git diff --check` produces no output.

- [ ] **Step 5: Commit**

Stage `.codex/config.toml`, `tests/workflow/shareme_sol_terra_workflow_test.py`, `docs/development/current-stage.md`, and this plan; commit with `fix: allow ShareMe root model selection`.
