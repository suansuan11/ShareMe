# ShareMe Sol–Terra Migration Design

## Goal

Replace ShareMe's active Sol–Luna workflow with a deterministic Sol–Terra
workflow that is compatible with the verified Sol-root subagent route.

## Scope

Migrate all active workflow entry points: project config, named agent files,
root instructions, skill package, validator, workflow test, and dynamic stage
handoff. Preserve existing plans and specifications that describe Luna as
historical decision evidence. Do not modify product source, global Codex
configuration, dependency caches, or past verification artifacts.

## Architecture

```text
Sol (gpt-5.6-sol / medium)
  ├─ terra_explorer (gpt-5.6-terra / medium / read-only)
  └─ terra_implementer (gpt-5.6-terra / medium / workspace-write)
```

The project retains a maximum of two active child agents for independent
read-only work. An implementer is the only writer for its implementation
scope; dependent review or testing waits until its write phase ends.

## Migration Rules

- Rename the active skill directory and skill identifier to
  `shareme-sol-terra`.
- Rename agent files and role names to `terra_explorer` and
  `terra_implementer`.
- Set the default subagent model and both role models to
  `gpt-5.6-terra` at medium reasoning.
- Replace active Luna language with Terra language, including dispatch,
  evidence, and handoff statements.
- Remove the obsolete Luna runtime limitation from the current stage and
  record the verified Sol-to-Terra route instead.
- Do not rename historical plan or specification files, and do not rewrite
  their historical Luna assertions.

## Verification

1. A focused workflow test first expects Terra names and model values, then
   fails against the Luna configuration.
2. The full workflow test and skill validator pass after migration.
3. A fresh ShareMe Sol/medium task starts `terra_explorer` for a read-only
   `client/rtc/desktop` analysis without model fallback.
4. Git status and diff checks show only active workflow migration files.

## Evidence Boundary

Static tests prove repository configuration and contract consistency. A fresh
task proves the current Desktop Sol-to-Terra dispatch route. Exact deployed
model identity remains unobservable unless Codex exposes it; report configured
model names separately from unobservable runtime internals.
