# ShareMe Dynamic Luna Model Routing Design

## Goal

Make the repository-owned Sol-Luna workflow actually route bounded Luna work
to a lower-cost available model when appropriate, without pinning a model name
in tracked workflow policy or weakening ShareMe's review and verification
requirements.

## Current evidence

- The root session currently uses a demanding Sol model, while the committed
  workflow contains no model override or model-selection procedure.
- A filesystem-read-only A/B check dispatched the same repository audit once
  with inherited settings and once with an explicit lower-cost model override.
  Both outputs correctly identified the Git baseline, Windows evidence, stale
  README state, and next development stage without new Git-visible repository
  changes. Ignored and external filesystem writes were not independently
  observed.
- The runtime accepted the explicit override. Exact per-agent token and credit
  totals were not exposed, so the experiment proves routing and adequate output
  quality, not the realized credit saving for that individual task.
- Current Codex documentation says explicit spawn settings take precedence over
  configured defaults, smaller models can extend usage limits, and subagents
  consume more tokens than comparable single-agent work. Therefore delegation
  must remain selective.

## Approaches considered

### 1. Dynamic explicit routing at dispatch time — selected

Sol inspects the model choices exposed by the current runtime and selects the
least expensive adequate model for each bounded Luna task. The repository
stores capability-tier rules and audit fields, not model slugs. This remains
portable across accounts, clients, macOS, Windows, and future model catalogs.

### 2. Tracked custom-agent files with pinned models — rejected

Role files would make selection deterministic, but a pinned model may be
unavailable on another account or become stale. It would contradict the source
prompt's requirement to query current support before selecting a model.

### 3. Global or project-wide default subagent model — rejected

A global default would affect unrelated repositories. A project default would
force the same model onto exploration, implementation, and difficult review,
discarding task-sensitive routing and making fallback less explicit.

## Routing contract

Sol remains the decision, architecture, integration, and acceptance owner.
Before each dispatch, Sol classifies the task and records these fields:

```text
Target capability tier:
Requested model:
Requested reasoning effort:
Selection reason:
Cost-tier basis:
Fallback or difference:
```

Every response also records `Actual model/fallback:` with the runtime-visible
accepted, rejected, or inherited outcome.

The model value is recorded in the live task contract, not in stable tracked
policy. Selection follows these rules:

1. Work directly when delegation would cost more coordination than it saves.
2. When current runtime/account cost-tier evidence exists, route bounded
   exploration, read-heavy scanning, routine test execution, and narrowly
   specified implementation to the lowest-cost available model with sufficient
   coding and tool capability. Without that evidence, select by capability only
   and make no expected or realized cost-saving claim.
3. Use balanced reasoning for ordinary Luna work and raise it only when the
   bounded task requires deeper edge-case analysis.
4. Keep ambiguous architecture, security, deletion, concurrency, lifetime,
   consistency, integration decisions, and final acceptance with Sol.
5. If the preferred model is unavailable or the override is rejected, inherit
   the parent model, record the requested difference and actual return outcome,
   and make no savings claim.
6. Never create duplicate agents merely to save credits. Parallel agents remain
   limited to independent work that materially improves latency or confidence.

Every existing Luna dispatch and return field remains mandatory. Model routing
does not grant broader filesystem, Git, cache, or product authority.

## Evidence and reporting

Three different claims must remain separate:

- **Routing verified:** the runtime accepted the explicit model override and
  returned a compliant result.
- **Quality verified:** Sol checked the result against exact repository facts
  and found no acceptance failure in the named task.
- **Credit saving measured:** exact comparable credit or token telemetry exists.

Without per-agent usage telemetry, report the third claim as unmeasured. Public
rate-card differences may support an expected unit-cost reduction, but never an
exact realized saving. Record failures, fallbacks, retries, and duplicate work
because they can erase the expected benefit.

## Repository changes

- Extend the workflow contract test first with required routing and reporting
  phrases.
- Add stable selection and audit rules to the ShareMe skill and role contract.
- Record the A/B result and its limitations in a verification document.
- Update the dynamic current-stage handoff with the delivered routing boundary.
- Do not add `.codex/config.toml`, global configuration, model slugs, pricing
  values, generated transcripts, or product-code changes.

## Verification

1. Observe the workflow contract test fail before implementation.
2. Make the minimum policy changes and pass the workflow test and skill
   validator.
3. Run a fresh lower-cost-model Luna audit using the complete role contract.
4. Sol independently checks its facts and Git-visible repository status, while
   reporting ignored and external filesystem observation limits.
5. Run independent specification and quality reviews.
6. Run the affected repository suites, `git diff --check`, and clean-status
   checks before integration.

## Rollback

The change is documentation and workflow policy only. Revert its focused
commits to restore inherited subagent behavior. No product data, dependency,
cache, global Codex configuration, or external service requires migration.
