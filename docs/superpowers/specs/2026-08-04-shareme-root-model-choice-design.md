# ShareMe Root Model Choice Design

## Goal

New ShareMe tasks must not be forced to start with `gpt-5.6-sol`.  The Codex
client or user-level configuration selects the root model from the models
available to that session.

## Scope

Remove only the project-level root-model settings from `.codex/config.toml`:

- `model = "gpt-5.6-sol"`
- `model_reasoning_effort = "medium"`

Keep the existing `[agents]` settings unchanged.  Terra remains the default
subagent model at medium reasoning effort, and the named Terra role files keep
their current read-only and workspace-write boundaries.

## Behavior

Project configuration no longer overrides root-model selection for newly
created ShareMe tasks.  The selected root model is therefore controlled by the
Codex client and any applicable user-level configuration.

This change does not add a fallback, change the available model catalog, or
change child-agent routing.  A model unavailable to a given Codex session
remains unavailable; the project simply no longer pins the root task to Sol.

## Verification

Static workflow coverage must prove that `.codex/config.toml` has no root
`model` or `model_reasoning_effort` entry, while retaining the Terra child
defaults.  The existing project skill validation must still pass.

Runtime model availability and the exact model selected in a future task are
environment-dependent, because Codex exposes them through the client/session
routing layer rather than this repository configuration.

## Rollback

Restore the two removed root settings in `.codex/config.toml` to pin new tasks
back to Sol at medium reasoning effort.  No agent-role files or source code
need to be reverted.
