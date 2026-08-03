# ShareMe Deterministic Sol-Luna Workflow Design

## Goal

Replace the document-only, dynamically routed Sol-Luna policy with a small,
project-scoped Codex configuration that makes new ShareMe tasks use Sol as the
main agent and Luna as the bounded default worker.  The workflow must reduce
unnecessary delegation without changing product code, global Codex settings,
dependency caches, or external services.

## Problem

The current workflow requires per-dispatch model selection metadata, permits
inheritance from the parent when an override is unavailable, and requires two
review gates for every substantive stage.  It therefore has no deterministic
default worker model and adds coordination tokens even to routine tasks.  Its
contract test also requires `.codex/config.toml` to be absent, so it verifies
wording rather than the runtime configuration Codex supports.

## Selected design

### Project configuration

Add `.codex/config.toml` with these defaults:

```toml
model = "gpt-5.6-sol"
model_reasoning_effort = "medium"

[agents]
enabled = true
max_concurrent_threads_per_session = 1
default_subagent_model = "gpt-5.6-luna"
default_subagent_reasoning_effort = "medium"
```

The project is already trusted locally.  Codex applies this project layer to
new ShareMe sessions before the user configuration, while explicit per-turn or
spawn overrides remain higher precedence.  The configuration neither changes
the model of an already-running session nor changes any other repository.

### Custom agents

Add two project-scoped custom agent files:

- `luna_explorer`: read-only evidence gathering; it cannot edit, commit,
  change dependencies, or prescribe unrequested redesign.
- `luna_implementer`: one bounded implementation owner; it may edit only its
  frozen target, must run named tests, and cannot commit, expand scope, or
  perform cleanup.

Both agents pin Luna at medium reasoning.  No `sol_escalation` agent is added:
the main Sol agent already owns high-risk decisions, and creating a second Sol
thread would add cost without a distinct responsibility.  A separate tester
agent is also omitted; Sol runs focused routine verification directly and
delegates only independent, unusually noisy test or log analysis.

### Delegation policy

The reduced policy is:

1. Sol works directly by default, including small changes, ordinary tests,
   formatting, and routine review.
2. Sol may start one Luna agent only when a bounded task materially reduces
   main-thread noise or latency.
3. The configured maximum is one active subagent.  Parallel work requires an
   explicit temporary override by the user and only applies to independent,
   read-only work.
4. A single implementation scope has one writer.  Architecture, security,
   deletion, migrations, concurrency, and final acceptance remain with Sol.
5. A short task prompt states role, target, allowed scope, forbidden scope,
   acceptance evidence, and return summary.  It does not repeat model prices,
   capability tiers, fallback fields, or a fixed multi-section response
   template.

This policy deliberately treats delegation as a quality or elapsed-time tool,
not as an automatic cost-saving mechanism.  Measured credit saving remains
unimplemented unless comparable per-agent usage telemetry becomes available.

### Repository instructions and evidence

Shorten `AGENTS.md`, `SKILL.md`, and the role contract around the selected
policy while preserving ShareMe's platform-evidence, cache-preservation,
single-writer, and focused-commit requirements.  Replace the old dynamic
routing design and verification claims with an honest migration note: the old
audit proved one explicit override, not a lasting saving or default routing.
Update the current-stage handoff only after implementation verification.

## Rejected alternatives

- **Keep dynamic selection for every dispatch.** It preserves flexibility but
  repeats selection reasoning and permits expensive inherited workers.
- **Use a global configuration.** It changes other repositories and is outside
  the confirmed scope.
- **Create four named roles with mandatory independent reviews.** It increases
  token and coordination costs without improving routine ShareMe work.

## Acceptance and verification

The implementation is accepted when:

1. The project configuration and both custom agent files are present and parse
   under the installed Codex strict configuration check.
2. The workflow contract test first fails for the required configuration, then
   passes after the implementation and rejects a missing or altered critical
   setting.
3. The repository skill validator passes, the workflow remains portable, and
   `git diff --check` passes.
4. The focused workflow test, portable C++ suite, and Go race and vet suite
   pass where the current macOS environment supports them.
5. The final report labels configuration parsing as verified, activation in a
   newly created ShareMe session as environment-dependent unless directly
   observed, and credit savings as unmeasured.

## Rollback

Revert the focused workflow commit.  The change has no product-data, media,
cache, service, or global-configuration migration.
