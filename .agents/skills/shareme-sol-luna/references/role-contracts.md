# Sol-Luna Role Contracts

Sol owns requirements, scope, architecture, risk, integration, verification, Git, and the final claim. Luna output is evidence that Sol must inspect.

```text
Default active Luna count: <= 2
Hard maximum: 3
Concurrent writers in overlapping scope: 0
Implementers for one overlapping scope: 1
```

Parallel work is limited to independent read-only discovery or independent verification. Keep implementation sequential even when scopes appear independent.

## Dynamic model routing

Before every Luna dispatch, Sol inspects the model choices exposed by the current
runtime and selects the least expensive adequate capability tier. Skip
delegation when coordination cost exceeds its benefit. Use low-cost routing for
bounded exploration, routine tests, and narrowly specified implementation;
keep high-risk judgment and final acceptance with Sol. Request an explicit
model and reasoning value when the runtime supports them. If an override is
unavailable or rejected, inherit the parent, record the fallback, and make no
savings claim.
`Cost-tier basis:` records current runtime/account evidence for relative cost
ordering. If that evidence is absent, cost ordering is unverified.
Without a cost-tier basis, select by capability only and make no expected or realized cost-saving claim.
Never claim measured credit savings without per-agent usage telemetry.
Never create duplicate agents merely to save credits.

Record this live dispatch metadata in the exact dispatch template; stable policy
never pins model slugs or prices. Use balanced reasoning for ordinary Luna work;
raise it only for a bounded task that needs deeper edge-case analysis. Routing
does not broaden filesystem, Git, cache, or product authority.

## Decision table

| Work shape | Role behavior |
|---|---|
| Read-only discovery | Dispatch Luna Explorer with a bounded question; parallelize only independent reads |
| Tightly coupled small work | Sol performs it directly |
| Independent implementation | Dispatch one Luna Implementer after freezing its contract; Sol reviews the diff |
| Independent review | Filesystem-read-only Tester/Reviewer; no writes |
| Independent test execution | Source-read-only Tester/Reviewer; builds only with explicit ignored build-output scope |
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
Target capability tier:
Requested model:
Requested reasoning effort:
Selection reason:
Cost-tier basis:
Fallback or difference:
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
Actual model/fallback:
```

Every Luna response states in `Actual model/fallback:` whether the explicit
request was accepted, rejected, or fell back. Report only the runtime-visible
outcome; do not imply backend telemetry the agent cannot observe. Sol verifies
each command, test claim, and diff before treating the response as evidence.
