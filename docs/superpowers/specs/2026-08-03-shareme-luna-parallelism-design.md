# ShareMe Luna Parallelism Design

## Goal

Keep the existing Sol and Luna model configuration while allowing up to two
independent Luna subagents in a new trusted ShareMe task. The workflow must
avoid concurrent writes, preserve direct-first Sol work, and make no claim that
Luna is available until the current runtime actually accepts it.

## Selected design

Change only the project child-thread cap from one to two:

```toml
[agents]
max_concurrent_threads_per_session = 2
default_subagent_model = "gpt-5.6-luna"
default_subagent_reasoning_effort = "medium"
```

Keep the two existing project role files pinned to `gpt-5.6-luna` at medium
reasoning. Luna remains the actual requested execution model; Terra is neither
a fallback nor presented as Luna.

## Dispatch rules

Sol works directly for small, coupled, routine, or ordinary-test work. When
delegation materially improves latency or keeps noisy output out of Sol's
context, it may run at most two Luna agents only when their work is independent.

- Two Explorers may investigate disjoint, read-only questions in parallel.
- An Explorer and a read-only test or log-analysis task may run in parallel
  when neither depends on the other or on uncommitted writes.
- A single Implementer owns an implementation scope. No second writer may run
  against that scope, and review/test work that depends on its diff starts only
  after its write phase completes.
- Sol reviews all agent output, Git state, diffs, and test evidence before
  acceptance.

If the runtime does not list or accept Luna, Sol does not substitute Terra or
misreport the model. Sol performs the bounded task directly and records the
Luna route as environment-dependent. The two-agent cap is therefore a maximum,
not a requirement to delegate.

## Repository changes

- Update `.codex/config.toml`, the ShareMe skill, role contract, and workflow
  test from one active Luna to two independent active Lunas.
- Preserve the Luna model fields in the configuration and both role files.
- Update the current-stage handoff with the new concurrency policy and the
  runtime-availability boundary.

## Acceptance

1. The workflow test first fails when it expects two active Lunas but the
   configuration and policy still specify one.
2. The updated static contract verifies the cap of two, Luna model fields, and
   the single-writer rule.
3. The repository skill validator, workflow suite, `git diff --check`, portable
   C++ suite, and Go race/vet suite pass on macOS.
4. A runtime two-Explorer check is performed only if a fresh trusted ShareMe
   task exposes Luna. Otherwise report it as environment-dependent, not failed
   workflow behavior or evidence that Terra was used.

## Out of scope

This stage does not change global Codex configuration, account model access,
model names, product code, movie-audio worktrees, dependency caches, push,
merge, or deployment.

## Rollback

Revert the focused workflow commit to restore the one-child cap. No product
data, dependency cache, external service, or global configuration changes.
