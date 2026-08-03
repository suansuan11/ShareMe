# ShareMe Agent Instructions

Before planning, diagnosing, reviewing, changing, testing, or continuing this
repository, read `.agents/skills/shareme-sol-luna/SKILL.md` completely and
follow it. Then read `docs/development/current-stage.md` and verify its dynamic
claims against current Git and code before relying on them.

Sol is the root decision and acceptance owner. Luna agents perform bounded
exploration, implementation, or testing only through the role contract.
Subagent output is evidence for Sol to inspect, never completion by itself.

Do not use concurrent writers on overlapping repository state. Do not stop a
continuation request after exploration or a small internal task; finish the
defined verifiable stage. Do not expand scope, perform destructive cleanup,
push, merge, deploy, or claim an untested platform without user authority.

Create focused, coherent commits. Exclude generated output, dependency caches,
secrets, local configuration, IDE state, unrelated files, and unrelated user
changes. Inspect Git state and the staged diff before committing.

Report evidence with the skill's honest verification labels: verified,
partial, environment-dependent, or unimplemented. Name the platform, scope,
and exact evidence behind every completion claim.

For repository-external libwebrtc cache preservation and all detailed cache
checks, follow the project contract referenced by the skill. Do not copy or
weaken that procedure here.
