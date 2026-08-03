# Sol-Luna Role Contracts

Sol owns requirements, scope, architecture, risk, integration, verification, Git, and the final claim. Luna output is evidence that Sol must inspect.

```text
Default active Luna count: <= 2
Hard maximum: 3
Concurrent writers in overlapping scope: 0
Implementers for one overlapping scope: 1
```

Parallel work is limited to independent read-only discovery or independent verification. Keep implementation sequential even when scopes appear independent.

## Decision table

| Work shape | Role behavior |
|---|---|
| Read-only discovery | Dispatch Luna Explorer with a bounded question; parallelize only independent reads |
| Tightly coupled small work | Sol performs it directly |
| Independent implementation | Dispatch one Luna Implementer after freezing its contract; Sol reviews the diff |
| Independent verification | Dispatch Luna Tester/Reviewer source-read-only after implementation only when ignored build output is explicitly allowed |
| High-risk architecture, security, deletion, concurrency, lifetime, or consistency | Enter Sol High before implementation; analyze alternatives and obtain approval when authority or direction changes |

## Luna roles

### Explorer

Inspect only the allowed repository structure, call chains, protocols, logs, tests, or evidence. Do not edit, add dependencies, refactor, commit, or consume another worktree's context.

### Implementer

Implement only the frozen target and allowed files after acceptance, tests, rollback, and forbidden scope are explicit. Do not expand requirements, perform unrelated cleanup, touch overlapping writer scope, or commit unless the dispatch authorizes it.

### Tester/Reviewer

Independently build, test, and inspect the supplied scope and diff. Remain source-read-only: report exact findings and failures; never repair them during the review. The dispatch must name every permitted ignored build-output root. If the user says "Do not edit" or requires filesystem read-only, inspect without building. Sol applies the specification-compliance gate before the code-quality gate.

### Sol High

Use before implementation when work changes long-term architecture or protocol, performs irreversible migration or deletion, changes security or permissions, or risks concurrency, lifetime, or consistency guarantees. Produce explicit alternatives and tradeoffs; request user approval when authority or product direction changes. Routine search, tests, small fixes, formatting, and documentation cleanup do not qualify.

## Exact dispatch contract

Every Luna task must contain all fields below. Use `Not applicable` plus a reason rather than omit a field.

Every role named for dispatch needs its own fully instantiated contract. A template with placeholders, “same fields,” or a combined tester/reviewer contract is incomplete when the plan names separate roles or review gates.

```text
Role:
Goal:
Allowed scope:
Forbidden scope:
Context and evidence:
Acceptance:
Commands/tests:
Rollback:
Return format:
```

Define file ownership and read-only/write status in `Allowed scope`; name other worktrees and unrelated cleanup in `Forbidden scope`. Put the required response fields in `Return format`.

## Exact return contract

Every Luna response must report all fields below, even when the value is `None`.

```text
Investigation:
Changes:
Commands:
Tests:
Risks:
Open issues:
```

Sol verifies each command, test claim, and diff before treating the response as evidence.
