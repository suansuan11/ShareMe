# ShareMe Development Contracts

## Purpose

These rules keep independently developed modules integrable. “Agent” means any
human or automated contributor working within an assigned module.

## Directory Ownership

| Area | Primary responsibility |
| --- | --- |
| `client/core/room` | room lifecycle and role state |
| `client/core/signaling` | signaling-domain commands and events |
| `client/core/rtc` | transport-facing core contracts |
| `client/core/sync` | clocks, reports, and correction policy |
| `client/core/metrics` | metric types and aggregation |
| `client/media/demux`, `decode`, `render`, `subtitle` | file playback pipeline |
| `client/media/encode` | hardware/software encoder adapters |
| `client/media/audio` | movie PCM and device-independent audio logic |
| `client/capture` | fallback screen and process audio capture |
| `client/platform` | native platform services |
| `client/app` | Qt facade, models, and QML |
| `server` | signaling, rooms, auth, and ICE configuration |
| `deploy` | TURN, proxy, TLS, and deployment manifests |
| `tests` | cross-component, network, synchronization, and endurance tests |
| `docs` and root build files | architecture/integration owner |

Contributors may add tests for their module. A change outside the assigned area
must be limited to the smallest contract or build update required.

## Contract-First Changes

A cross-module change is split into focused commits:

1. update the owning header or protocol document and its contract tests;
2. update the provider implementation;
3. update consumers;
4. run portable tests and affected platform/integration tests.

Callers must not include an adapter's private headers. Native handles cross a
boundary only through an explicitly documented opaque type.

## Core Rules

- C++20; no compiler extensions in portable targets.
- Public APIs use `shareme` namespaces and explicit units in names.
- Time values crossing a boundary use `std::chrono` or a name ending in the
  unit, such as `rendered_pts_ms`.
- Ownership uses values and smart pointers; owning raw pointers are forbidden.
- Queues have fixed positive capacity, overload policy, owner, and metrics.
- Threads have a stop path and are joined before their dependencies die.
- Unsupported hardware is reported; CPU fallback requires an explicit user
  choice.
- QML does not process media frames or call FFmpeg/libwebrtc.

## Test Contract

Behavior changes follow red-green-refactor:

1. add one focused test and observe the expected failure;
2. add the minimum implementation;
3. run the focused test;
4. run the containing suite;
5. refactor only while green.

Each change records the exact local command run. Windows-specific claims require
a Windows run. Hardware performance claims require the named device, driver,
resolution, frame rate, codec, and sample duration.

## Commit Contract

Commits contain one reviewable concern and use conventional prefixes:

- `docs:` architecture and protocol contracts;
- `build:` build system or dependency policy;
- `feat(scope):` product behavior;
- `fix(scope):` defect correction with regression test;
- `test:` test infrastructure or scenarios;
- `ci:` automation;
- `chore:` repository maintenance.

Do not commit build output, dependency caches, media samples, logs, dumps,
secrets, IDE state, generated credentials, or unrelated local files.

## Integration Gates

Before integration:

- portable configure, build, and CTest pass;
- affected Windows Release targets compile;
- contract documents match public headers and serialized messages;
- queue capacities and shutdown order are visible in review;
- no silent codec fallback exists;
- the diff contains no unrelated files;
- performance status distinguishes measured, unmeasured, and environment-bound
  results.
