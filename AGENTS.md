# ShareMe Agent Instructions

For a ShareMe change, diagnosis, review, test, plan, or continuation, read
`.agents/skills/shareme-sol-terra/SKILL.md` and
`docs/development/current-stage.md` first. Verify dynamic handoff claims against
current source and Git before relying on them.

Sol owns scope, architecture, integration, verification, Git, and completion.
Sol works directly by default. Use at most two independent Terra agents only
for bounded work that materially reduces noise or elapsed time. Parallel work
is limited to independent read-only exploration, testing, or log analysis; the
project configuration provides `terra_explorer` and `terra_implementer`, and
one writer owns an overlapping implementation scope.

Do not expand scope, use concurrent writers, perform destructive cleanup,
push, merge, deploy, or claim untested platform behavior without user
authority. Keep commits focused; exclude generated output, dependency caches,
secrets, local settings, IDE state, and unrelated user changes.

Report evidence as verified, partial, environment-dependent, or unimplemented,
with platform and exact proof. Preserve the external libwebrtc cache and follow
the project contract for its detailed safeguards.
