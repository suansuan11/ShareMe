# ShareMe Phase 0 Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the documented architecture, portable C++20 build, bounded media queue contract, synchronization decision logic, and cross-platform CI needed before the three Phase 0 media demonstrations.

**Architecture:** Keep `client/core` free of Qt, FFmpeg, libwebrtc, GPU, and operating-system dependencies. Express queue overload and playback correction as deterministic C++ contracts with CTest coverage, while platform and media integrations remain opt-in adapters in later plans.

**Tech Stack:** C++20, CMake 3.24+, Ninja, CTest, GitHub Actions, Markdown

---

## File Map

- `docs/architecture.md`: component boundaries, dependency direction, runtime ownership, and media flow.
- `docs/protocols.md`: versioned signaling and data-channel message contracts.
- `docs/agent-contracts.md`: directory ownership and cross-module change rules.
- `docs/performance-targets.md`: first-release targets and measurement definitions.
- `CMakeLists.txt`: root project options and portable-core entry point.
- `CMakePresets.json`: shared debug, release, build, and test presets.
- `cmake/ShareMeWarnings.cmake`: compiler warning policy.
- `client/core/CMakeLists.txt`: portable core targets.
- `client/core/include/shareme/core/bounded_queue.hpp`: fixed-capacity overload contract.
- `client/core/include/shareme/core/sync_controller.hpp`: synchronization types and API.
- `client/core/src/sync_controller.cpp`: deterministic correction decision implementation.
- `tests/core/bounded_queue_test.cpp`: queue behavior executable.
- `tests/core/sync_controller_test.cpp`: synchronization boundary executable.
- `.github/workflows/core-ci.yml`: macOS ARM64-compatible and Windows x64 foundation build.

### Task 1: Publish Architecture and Cross-Module Contracts

**Files:**
- Create: `docs/architecture.md`
- Create: `docs/protocols.md`
- Create: `docs/agent-contracts.md`
- Create: `docs/performance-targets.md`

- [ ] **Step 1: Write the architecture contract**

Document the host/viewer flow, inward dependency direction, facade boundary,
track separation, bounded queue policies, worker ownership, and shutdown order.
State that media never traverses the signaling server.

- [ ] **Step 2: Write the protocol contract**

Define this envelope and the Phase 0/1 messages:

```json
{
  "version": 1,
  "type": "playout-report",
  "roomId": "7K4M9Q",
  "sequence": 42,
  "payload": {
    "renderedPtsMs": 125430,
    "bufferMs": 160,
    "receiveTimeMs": 9865321
  }
}
```

Specify validation limits, sequence handling, SDP/ICE payload ownership, and
that timestamps use signed 64-bit milliseconds on one media timeline.

- [ ] **Step 3: Write ownership and performance contracts**

Assign each top-level directory one owner, require contract-first cross-module
changes, and define measurement points for connection time, media latency,
voice latency, A/V error, host/viewer timeline error, drops, and resource use.

- [ ] **Step 4: Verify and commit the documents**

Run:

```bash
rg -n "TBD|TODO|待定" docs/architecture.md docs/protocols.md \
  docs/agent-contracts.md docs/performance-targets.md
git diff --check
```

Expected: the search prints nothing and `git diff --check` exits zero.

Commit:

```bash
git add docs/architecture.md docs/protocols.md docs/agent-contracts.md docs/performance-targets.md
git commit -m "docs: establish architecture and protocol contracts"
```

### Task 2: Create the Portable C++ Build Baseline

**Files:**
- Modify: `.gitignore`
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `cmake/ShareMeWarnings.cmake`
- Create: `client/core/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/core/CMakeLists.txt`

- [ ] **Step 1: Add the root project and options**

Use this root shape:

```cmake
cmake_minimum_required(VERSION 3.24)
project(ShareMe VERSION 0.1.0 LANGUAGES CXX)

option(SHAREME_BUILD_TESTS "Build ShareMe tests" ON)
option(SHAREME_ENABLE_QT "Enable Qt application targets" OFF)
option(SHAREME_ENABLE_FFMPEG "Enable FFmpeg media targets" OFF)
option(SHAREME_ENABLE_WEBRTC "Enable libwebrtc targets" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ShareMeWarnings)

add_subdirectory(client/core)

if(SHAREME_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Add warning and target defaults**

Create `shareme_set_project_warnings(target)` with `/W4 /permissive-` on MSVC
and `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` elsewhere.
Require `cxx_std_20` per target and disable compiler extensions.

- [ ] **Step 3: Add shared presets**

Add `dev`, `release`, `build-dev`, `build-release`, `test-dev`, and
`dev-workflow` presets using Ninja and `${sourceDir}/build/${presetName}`.
Set tests to output failures. Ignore `CMakeUserPresets.json`.

- [ ] **Step 4: Verify configure/build/test**

Run:

```bash
cmake --preset dev
cmake --build --preset build-dev
ctest --preset test-dev
```

Expected: configure and build exit zero; CTest reports no tests or all current
tests passing.

- [ ] **Step 5: Commit the build baseline**

```bash
git add .gitignore CMakeLists.txt CMakePresets.json cmake client/core/CMakeLists.txt tests
git commit -m "build: add portable C++ project baseline"
```

### Task 3: Implement the Bounded Queue Contract with TDD

**Files:**
- Create: `client/core/include/shareme/core/bounded_queue.hpp`
- Create: `tests/core/bounded_queue_test.cpp`
- Modify: `client/core/CMakeLists.txt`
- Modify: `tests/core/CMakeLists.txt`

- [ ] **Step 1: Write the failing queue tests**

The test executable must verify:

```cpp
using shareme::core::BoundedQueue;
using shareme::core::OverflowPolicy;

BoundedQueue<int> video_queue{2, OverflowPolicy::drop_oldest};
REQUIRE(video_queue.push(1));
REQUIRE(video_queue.push(2));
REQUIRE(video_queue.push(3));
REQUIRE(video_queue.pop() == std::optional<int>{2});
REQUIRE(video_queue.dropped_count() == 1);

BoundedQueue<int> audio_queue{1, OverflowPolicy::reject_newest};
REQUIRE(audio_queue.push(7));
REQUIRE_FALSE(audio_queue.push(8));
REQUIRE(audio_queue.pop() == std::optional<int>{7});
REQUIRE(audio_queue.dropped_count() == 1);
```

Also verify zero capacity throws `std::invalid_argument`, FIFO ordering,
`size()`, `capacity()`, `empty()`, and `clear()`.

- [ ] **Step 2: Run the queue test and verify RED**

Run:

```bash
cmake --build --preset build-dev --target shareme_bounded_queue_test
```

Expected: build fails because `shareme/core/bounded_queue.hpp` does not exist.

- [ ] **Step 3: Implement the minimal queue**

Implement a thread-safe template backed by `std::deque<T>` and `std::mutex`.
The public API is:

```cpp
enum class OverflowPolicy { drop_oldest, reject_newest };

template <typename T>
class BoundedQueue {
public:
  BoundedQueue(std::size_t capacity, OverflowPolicy policy);
  [[nodiscard]] bool push(T item);
  [[nodiscard]] std::optional<T> pop();
  void clear();
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::size_t capacity() const noexcept;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::uint64_t dropped_count() const;
};
```

Increment `dropped_count` for either overload policy. `clear()` removes queued
items but does not erase historical drop metrics.

- [ ] **Step 4: Run the queue test and verify GREEN**

Run:

```bash
cmake --build --preset build-dev --target shareme_bounded_queue_test
ctest --test-dir build/dev -R bounded_queue --output-on-failure
```

Expected: build exits zero and one queue test passes.

- [ ] **Step 5: Commit the queue contract**

```bash
git add client/core tests/core
git commit -m "feat(core): add bounded media queue"
```

### Task 4: Implement Synchronization Decisions with TDD

**Files:**
- Create: `client/core/include/shareme/core/sync_controller.hpp`
- Create: `client/core/src/sync_controller.cpp`
- Create: `tests/core/sync_controller_test.cpp`
- Modify: `client/core/CMakeLists.txt`
- Modify: `tests/core/CMakeLists.txt`

- [ ] **Step 1: Write the failing synchronization tests**

Verify exact boundaries and both signs of the delta:

```cpp
SyncController controller;

REQUIRE(controller.decide(49).action == SyncAction::none);
REQUIRE(controller.decide(50).action == SyncAction::adjust_buffer);
REQUIRE(controller.decide(119).action == SyncAction::adjust_buffer);
REQUIRE(controller.decide(120).action == SyncAction::adjust_rate);
REQUIRE(controller.decide(299).action == SyncAction::adjust_rate);
REQUIRE(controller.decide(300).action == SyncAction::hard_resync);
REQUIRE(controller.decide(-300).action == SyncAction::hard_resync);
```

For rate adjustment, require `0.98 <= playback_rate <= 1.02`, with a viewer
behind the host producing a rate below `1.0` for delayed host playout. Require
`target_delay_delta_ms` to preserve the signed correction direction.

- [ ] **Step 2: Run the synchronization test and verify RED**

Run:

```bash
cmake --build --preset build-dev --target shareme_sync_controller_test
```

Expected: build fails because `sync_controller.hpp` does not exist.

- [ ] **Step 3: Implement the minimal controller**

Use these public types:

```cpp
enum class SyncAction { none, adjust_buffer, adjust_rate, hard_resync };

struct SyncDecision {
  SyncAction action;
  std::int64_t target_delay_delta_ms;
  double playback_rate;
};

class SyncController {
public:
  [[nodiscard]] SyncDecision decide(std::int64_t viewer_delta_ms) const noexcept;
};
```

Apply absolute thresholds `[0, 50)`, `[50, 120)`, `[120, 300)`, and
`[300, infinity)`. Clamp rate adjustment to `[0.98, 1.02]`; return `1.0` for
non-rate actions.

- [ ] **Step 4: Run synchronization and full tests**

Run:

```bash
cmake --build --preset build-dev
ctest --preset test-dev
```

Expected: both core tests pass with zero failures.

- [ ] **Step 5: Commit synchronization logic**

```bash
git add client/core tests/core
git commit -m "feat(core): add playback synchronization decisions"
```

### Task 5: Add Cross-Platform Foundation CI

**Files:**
- Create: `.github/workflows/core-ci.yml`
- Create: `README.md`

- [ ] **Step 1: Add the CI matrix**

Run the same preset workflow on `macos-15` and `windows-2025`:

```yaml
- name: Configure
  run: cmake --preset dev
- name: Build
  run: cmake --build --preset build-dev
- name: Test
  run: ctest --preset test-dev
```

Install Ninja with the runner package manager only when it is not already
available. Trigger on pushes and pull requests that touch CMake, C++, tests,
docs, or the workflow.

- [ ] **Step 2: Write the contributor entry point**

Document prerequisites, preset commands, optional dependency flags, branch
scope, Windows verification status, and links to the four contract documents.
Do not claim any of the three technical demonstrations are implemented.

- [ ] **Step 3: Run local verification**

Run:

```bash
cmake --workflow --preset dev-workflow
git diff --check
git status --short
```

Expected: configure, build, and tests exit zero; diff check is clean; status
contains only the intended README and workflow changes.

- [ ] **Step 4: Commit CI and contributor guidance**

```bash
git add .github/workflows/core-ci.yml README.md
git commit -m "ci: verify portable core on macOS and Windows"
```

### Task 6: Audit the Foundation Slice

**Files:**
- Modify only files that fail the checks below.

- [ ] **Step 1: Verify design and plan coverage**

Confirm every acceptance item in
`docs/superpowers/specs/2026-07-28-phase0-foundation-design.md` is represented
by implementation, tests, documentation, or an explicit Windows CI job.

- [ ] **Step 2: Run the complete evidence suite**

```bash
cmake --fresh --preset dev
cmake --build --preset build-dev
ctest --preset test-dev
git diff --check
git status --short --branch
git log --oneline --decorate -n 8
```

Expected: clean configure/build, two passing tests, no whitespace errors, a
clean worktree, and focused commits for design, plan, contracts, build, queue,
sync, and CI.

- [ ] **Step 3: Record honest platform status**

Report the local macOS result separately from Windows CI status. Do not mark
Windows verification complete until GitHub Actions or the user's Windows
machine has produced a successful run.
