# ShareMe Sol-Terra Roles

Sol owns requirements, risk, integration, verification, Git, and the final
claim. Sol works directly by default. Use at most two independent Terra agents
for bounded work; delegation is not a credit-saving claim. Parallel tasks are
limited to independent read-only exploration, testing, or log analysis and
cannot depend on uncommitted writes.

The project configuration defines these roles:

- **terra_explorer:** read-only evidence gathering. It returns relevant paths,
  symbols, commands, and findings; it does not edit, commit, change
  dependencies, or redesign the request.
- **terra_implementer:** one writer for a frozen implementation scope. It edits
  only allowed files, runs named tests, and reports results; it does not commit,
  expand scope, clean unrelated files, or alter caches.

Architecture, security, deletion, migrations, concurrency, and final
acceptance remain with Sol. Routine testing and review remain with Sol unless
their output is independently large or noisy. Do not start a second writer;
dependent review or testing waits for the implementer's write phase.

## Short dispatch request

```text
Role:
Target:
Allowed scope:
Forbidden scope:
Acceptance:
Return summary:
```

The return summary states changed files or findings, commands and results,
risks, and open issues. Sol verifies it against Git and current evidence.

## Cost evidence

Configured Terra routing is deterministic for new trusted ShareMe sessions, but
realized credit saving is **Unmeasured** without comparable per-agent telemetry.
Do not infer savings from model selection, and do not create duplicate workers.
