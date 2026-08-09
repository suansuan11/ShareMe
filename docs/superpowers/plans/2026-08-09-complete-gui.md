# ShareMe Complete GUI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the approved Calm Dark, focus-stage ShareMe desktop GUI with real create/join, primary-audio controls, leave/rejoin lifecycle, truthful state recovery, responsive QML, and preserved CLI automation.

**Architecture:** Add a tested application/session model and a stable `ShareMeAppController` above the existing one-session `RtcDemoController`. Keep signaling and media ownership in `RtcDemoController`/`SignaledPeer`, expose only narrow runtime audio and lifecycle operations, and split the monolithic QML into focused pages/components. Explicit CLI configurations still autostart; no-argument launch enters the product home flow.

**Tech Stack:** C++20, Qt 6 Core/Gui/QML/Quick/QuickControls2/Multimedia/Test, WebRTC native APIs, CMake/CTest, Python CLI contract tests, QML offscreen smoke.

## Global Constraints

- Preserve the external WebRTC cache at `/Users/dio/Library/Caches/ShareMe/webrtc`; do not modify, clean, reconfigure, or commit it.
- Keep a single `VideoOutput`; do not add continuous decorative animations, duplicate previews, per-frame QML text updates, or independent diagnostic polling.
- Do not reduce capture resolution, frame rate, bitrate, codec quality policy, or media gates to make the GUI pass.
- Existing explicit CLI calls and smoke runners must retain deterministic autostart and machine-readable output.
- UI controls must invoke real supported behavior or expose an explicit unavailable reason; never simulate success.
- File sharing, system audio, remote input, TURN, HDR, 4K60 optimization, and movie hard-resync remain out of scope.
- Generated screenshots, media, logs, build products, caches, secrets, local settings, and IDE state must remain untracked.
- Platform claims must be labeled verified, partial, environment-dependent, or unimplemented with exact evidence.

---

### Task 1: Typed launch and preflight model

**Files:**
- Create: `client/tools/rtc_demo/app_session_config.hpp`
- Create: `client/tools/rtc_demo/app_session_config.cpp`
- Create: `tests/rtc/app_session_config_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`

**Interfaces:**
- Produces: `enum class AppPage { home, preflight, calling, result }`
- Produces: `enum class PreflightMode { create_room, join_room }`
- Produces: `struct AppSessionConfig` containing server URL, role, room, source, screen profile, audio mode, native playout, and existing movie/measurement fields.
- Produces: `normalize_room_code(QString) -> QString` and `validate_interactive_config(const AppSessionConfig&) -> AppConfigValidation`.

- [ ] **Step 1: Write failing normalization and validation tests**

```cpp
REQUIRE(normalize_room_code("  kd-g lmd  ") == "KDG-LMD");
REQUIRE(!validate_interactive_config(viewer_with_empty_room).accepted);
REQUIRE(validate_interactive_config(valid_host).accepted);
REQUIRE(validate_interactive_config(valid_viewer).accepted);
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
cmake --preset call-dev
cmake --build --preset build-call-dev --target shareme_app_session_config_test
ctest --test-dir build/call-dev -R '^app_session_config$' --output-on-failure
```

Expected: build failure because `app_session_config.hpp` and its APIs do not exist.

- [ ] **Step 3: Implement the minimal typed model and validation**

```cpp
struct AppConfigValidation {
  bool accepted{};
  QString category;
  QString message;
};

[[nodiscard]] QString normalize_room_code(QString room);
[[nodiscard]] AppConfigValidation
validate_interactive_config(const AppSessionConfig &config);
```

Validation must reject malformed URLs, viewer sessions without a valid room,
unsupported interactive sources, and unavailable screen builds without
including local paths or credentials in its result.

- [ ] **Step 4: Run focused test and full existing config/CLI contracts GREEN**

Run the new CTest target and registered `rtc_demo_cli` test.
Expected: all selected tests pass with zero warnings/errors.

- [ ] **Step 5: Commit**

```bash
git add client/tools/rtc_demo/app_session_config.* \
  client/tools/rtc_demo/CMakeLists.txt tests/rtc/app_session_config_test.cpp \
  tests/rtc/CMakeLists.txt
git commit -m "feat: add typed GUI session configuration"
```

### Task 2: Real primary-audio toggles and idempotent session stop

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`
- Create: `tests/rtc/rtc_demo_control_state_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `SignaledPeer::set_local_audio_enabled(bool) -> bool`
- Produces: `SignaledPeer::set_remote_audio_enabled(bool) -> bool`
- Produces: controller properties `microphoneMuted`, `speakerMuted`, `stoppable`, and `sessionEnded`.
- Produces: controller invokables `setMicrophoneMuted(bool)`, `setSpeakerMuted(bool)`, and idempotent `stop()`.

- [ ] **Step 1: Write failing peer audio-control tests**

Add a real signaled-peer test proving operations reject an unstarted/stopped
peer, accept an active peer, and preserve the requested state across reads.
The mutation caught is a toggle that updates UI state without changing the
actual WebRTC track/playout state.

```cpp
REQUIRE(peer->start());
REQUIRE(peer->set_local_audio_enabled(false));
REQUIRE(!peer->local_audio_enabled());
REQUIRE(peer->set_remote_audio_enabled(false));
REQUIRE(!peer->remote_audio_enabled());
```

- [ ] **Step 2: Run `signaled_peer` and verify RED**

Expected: compile failure on the absent runtime-audio APIs.

- [ ] **Step 3: Implement signaling-thread-safe track/playout operations**

Use the owned WebRTC signaling thread and `BlockingCall`; reject after stop or
before the relevant peer/track exists. Local mute calls
`audio_track_->set_enabled(enabled)`. Remote control calls both the supported
peer playout boundary and `remote_audio_->set_enabled(enabled)` when present.
Atomics expose last accepted state without touching track objects off-thread.

- [ ] **Step 4: Run `signaled_peer` GREEN and repeat it 20 times**

Expected: every repetition passes without race, timeout, or lifecycle failure.

- [ ] **Step 5: Write failing controller control-state test**

Extract a small `RtcControlState` if required so mute labels and idempotent stop
can be tested without opening a real call:

```cpp
RtcControlState state;
REQUIRE(state.request_microphone_muted(true));
REQUIRE(state.microphone_muted());
REQUIRE(state.finish_session());
REQUIRE(!state.finish_session());
```

- [ ] **Step 6: Implement controller properties/invokables and safe stop**

Controller state changes only after the peer operation succeeds. `stop()` runs
on the Qt owner thread, is idempotent, reuses existing shutdown ordering, emits
state changes, and leaves the destructor safe after an explicit stop.

- [ ] **Step 7: Run focused controller, peer, and lifecycle tests GREEN**

Expected: focused CTests pass and signaled-peer lifecycle passes 20/20.

- [ ] **Step 8: Commit**

```bash
git add client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp \
  client/rtc/webrtc/src/signaled_peer.cpp client/tools/rtc_demo/rtc_demo_controller.* \
  tests/rtc/signaled_peer_test.cpp tests/rtc/rtc_demo_control_state_test.cpp \
  tests/rtc/CMakeLists.txt
git commit -m "feat: add live call audio and leave controls"
```

### Task 3: Application coordinator and interactive/CLI startup split

**Files:**
- Create: `client/tools/rtc_demo/shareme_app_controller.hpp`
- Create: `client/tools/rtc_demo/shareme_app_controller.cpp`
- Create: `client/tools/rtc_demo/app_preferences.hpp`
- Create: `client/tools/rtc_demo/app_preferences.cpp`
- Create: `client/tools/rtc_demo/launch_options.hpp`
- Create: `client/tools/rtc_demo/launch_options.cpp`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Create: `tests/rtc/shareme_app_controller_test.cpp`
- Create: `tests/rtc/app_preferences_test.cpp`
- Create: `tests/rtc/launch_options_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produces: `LaunchOptions { bool interactive; optional<AppSessionConfig> autostart; }`.
- Produces: `ShareMeAppController` properties `page`, `preflightMode`, `activeController`, `busy`, `errorCategory`, `errorMessage`, `serverUrl`, `roomCode`, `screenProfile`, `microphoneEnabled`, `speakerEnabled`.
- Produces invokables: `showCreateRoom()`, `showJoinRoom()`, `returnHome()`, `startCall()`, `leaveCall()`, `retryCall()`, and `copyRoomCode()`.
- Produces: `AppPreferences` storing only bounded UI preferences and at most one
  normalized recent room through an injected `QSettings` scope.
- Consumes: Task 1 config validation and Task 2 controller stop/control APIs.

- [ ] **Step 1: Write failing launch classification tests**

```cpp
REQUIRE(classify_launch(no_rtc_options).interactive);
REQUIRE(classify_launch(valid_explicit_host).autostart.has_value());
REQUIRE(!classify_launch(partial_explicit_options).accepted());
```

- [ ] **Step 2: Run focused launch test and verify RED**

Expected: compile failure because launch classification is absent.

- [ ] **Step 3: Extract parser result and preserve fail-closed CLI behavior**

No arguments enters interactive mode. Any explicit RTC option selects strict
CLI mode, so partial calls still return usage error. `--validate` never opens a
window and continues to use strict validation.

- [ ] **Step 4: Update Python CLI test and verify RED then GREEN**

Replace the old no-argument usage-error assertion with an offscreen launch
probe that expects the process to remain alive briefly, then terminate it. Keep
partial/invalid CLI cases returning 2 and all existing explicit validations.

- [ ] **Step 5: Write failing coordinator transition tests with an injected factory**

Use a minimal factory interface returning a real QObject session boundary, not
a mock of UI behavior. Assert observable page/controller lifecycle:

```cpp
app.showJoinRoom();
REQUIRE(app.page() == AppPage::preflight);
app.setRoomCode("abc234");
REQUIRE(app.startCall());
REQUIRE(app.page() == AppPage::calling);
app.leaveCall();
REQUIRE(app.page() == AppPage::home);
REQUIRE(factory.live_sessions() == 0);
```

- [ ] **Step 6: Implement coordinator ownership and typed error mapping**

The production factory constructs `RtcDemoController`. The coordinator exposes
a `QPointer<QObject>` active controller, disconnects it before deletion, and
maps sanitized controller categories to recovery actions.

- [ ] **Step 7: Wire `main.cpp` to one stable app controller**

Set QML initial property `appController`. Invoke coordinator autostart only for
validated explicit CLI configurations. Interactive no-argument launch starts
on `home` and defaults its development endpoint to
`ws://127.0.0.1:8080/v1/ws`.

- [ ] **Step 8: Run coordinator, launch, CLI, and shutdown tests GREEN**

Expected: focused tests pass; invalid CLI exits remain 2; interactive offscreen
launch has no QML root failure.

- [ ] **Step 9: Write RED/GREEN preferences tests**

Use a temporary INI file and the real `QSettings` adapter. Prove the most recent
normalized room is restored, `forgetRecentRoom()` removes it, a second room
replaces rather than accumulates history, and server credentials/movie paths
are never persisted.

- [ ] **Step 10: Commit**

```bash
git add client/tools/rtc_demo/{shareme_app_controller,launch_options,app_preferences}.* \
  client/tools/rtc_demo/main.cpp client/tools/rtc_demo/CMakeLists.txt \
  tests/rtc/{shareme_app_controller,launch_options,app_preferences}_test.cpp \
  tests/rtc/CMakeLists.txt tests/scripts/rtc_demo_cli_test.py
git commit -m "feat: add reusable GUI call lifecycle"
```

### Task 4: Calm Dark component system, home, and preflight

**Files:**
- Create: `client/tools/rtc_demo/qml/ShareMeTheme.qml`
- Create: `client/tools/rtc_demo/qml/PrimaryButton.qml`
- Create: `client/tools/rtc_demo/qml/IconControl.qml`
- Create: `client/tools/rtc_demo/qml/InfoRow.qml`
- Create: `client/tools/rtc_demo/qml/HomePage.qml`
- Create: `client/tools/rtc_demo/qml/PreflightPage.qml`
- Create: `client/tools/rtc_demo/qml/SettingsDialog.qml`
- Create: `client/tools/rtc_demo/qml/HelpDialog.qml`
- Create: `client/tools/rtc_demo/qml/StatusBanner.qml`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Create: `tests/scripts/gui_qml_contract_test.py`
- Modify: `tests/scripts/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 `ShareMeAppController` properties and invokables.
- Produces: reusable accessible controls and the `home`/`preflight` pages.

- [ ] **Step 1: Write failing executable QML contract tests**

The test launches the real QML module offscreen through a GUI-test option,
drives coordinator state, and asserts emitted state markers rather than
grepping source. It must catch a missing create action, invalid room acceptance,
or a QML loading warning.

- [ ] **Step 2: Run registered GUI QML contract and verify RED**

Expected: failure because the GUI-test option/pages/state markers do not exist.

- [ ] **Step 3: Implement theme and reusable controls**

Use frozen colors, 4/8/12/16/24 spacing, 8/12/16 radii, deterministic local
icons/primitives, focus visuals, accessible names, tooltips, and disabled
reasons. Do not add remote assets or continuous animation.

- [ ] **Step 4: Implement HomePage and PreflightPage**

Home shows Create, Join, Settings, Help, and at most one recent-room action with
a Forget affordance. Settings exposes only persisted theme/UI preferences plus
the development endpoint for interactive builds. Help explains the room flow,
permission recovery, and keyboard controls. Preflight binds only valid host or
viewer controls and displays capability readiness. All validation remains in
C++; QML only displays validation results.

- [ ] **Step 5: Implement root navigation in Main.qml**

Use the stable app controller and explicit page enum/string mapping. Preserve a
minimum window size, keyboard traversal, and visible focus. Do not construct or
destroy media objects from QML.

- [ ] **Step 6: Run QML contract, CLI, and offscreen load GREEN**

Expected: no QML warnings; home/preflight states and invalid-room path pass.

- [ ] **Step 7: Commit**

```bash
git add client/tools/rtc_demo/qml client/tools/rtc_demo/CMakeLists.txt \
  tests/scripts/gui_qml_contract_test.py tests/scripts/CMakeLists.txt
git commit -m "feat: add ShareMe home and call preflight UI"
```

### Task 5: Focus-stage call page and complete recovery states

**Files:**
- Create: `client/tools/rtc_demo/qml/CallPage.qml`
- Create: `client/tools/rtc_demo/qml/CallTopBar.qml`
- Create: `client/tools/rtc_demo/qml/CallControlDock.qml`
- Create: `client/tools/rtc_demo/qml/CallDetailsDrawer.qml`
- Create: `client/tools/rtc_demo/qml/VideoStage.qml`
- Create: `client/tools/rtc_demo/qml/RecoveryDialog.qml`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `tests/scripts/gui_qml_contract_test.py`
- Modify: `tests/rtc/shareme_app_controller_test.cpp`

**Interfaces:**
- Consumes: `activeController`, mute/leave invokables, typed state/error values,
  and all existing movie/diagnostic properties.
- Produces: one `VideoOutput`, a bottom control dock, optional drawer, movie
  controls, user-safe waiting/error/retry/leave flows.

- [ ] **Step 1: Extend failing GUI tests for host/viewer call behavior**

Exercise the real QML module in test mode and assert observable action results:
drawer opens/closes, microphone and speaker state changes, leave returns home,
compact layout switches mode, diagnostics default closed, and unavailable
controls report a reason.

- [ ] **Step 2: Run GUI test and verify RED for each behavior**

Expected: each new case fails on the missing call components/action result.

- [ ] **Step 3: Implement VideoStage and CallTopBar**

Create exactly one `VideoOutput`, preserve aspect fit, show distinct waiting,
local-share, remote-share, and unavailable states, and expose room-code copy
feedback without displaying raw transport data.

- [ ] **Step 4: Implement dock with real controls**

Bind microphone/speaker actions to Task 2 methods. Host share presentation is
enabled only when supported; viewer shows status without a false action. Leave
uses coordinator lifecycle and host confirmation for established rooms.

- [ ] **Step 5: Implement responsive details drawer**

At regular width it overlays from the right; at compact width it becomes a
bottom sheet. Render participant/role, profile, geometry, audio/route, network,
movie controls, and collapsed Advanced Diagnostics. Hidden diagnostics add no
timer and do not duplicate video.

- [ ] **Step 6: Implement recovery/result surfaces**

Map typed categories to retry/edit/continue-muted/open-guidance/home actions.
Unknown categories show a sanitized generic failure and the Advanced category,
never paths, SDP, ICE strings, or credentials.

- [ ] **Step 7: Run GUI/controller contracts and QML warning gate GREEN**

Expected: regular and compact states pass, controller lifecycle remains green,
and QML emits no binding/type/accessibility warnings.

- [ ] **Step 8: Commit**

```bash
git add client/tools/rtc_demo/qml client/tools/rtc_demo/CMakeLists.txt \
  tests/scripts/gui_qml_contract_test.py \
  tests/rtc/shareme_app_controller_test.cpp
git commit -m "feat: add complete focus-stage call interface"
```

### Task 6: Native interaction, regression, and performance gate

**Files:**
- Create: `scripts/run_gui_call_smoke.py`
- Create: `tests/scripts/gui_call_smoke_test.py`
- Modify: `tests/scripts/CMakeLists.txt`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produces: sanitized GUI smoke summary with lifecycle/action/state counters and
  bounded CPU/RSS samples.
- Consumes: existing signaling server and explicit GUI automation/test hooks.

- [ ] **Step 1: Write failing runner contract tests**

Use temporary fake host/viewer processes to verify early-exit propagation,
timeouts, missing state markers, action failure, artifact path protection, and
sanitized output. Do not assert only that fakes were invoked.

- [ ] **Step 2: Run runner tests and verify RED**

Expected: import/behavior failure because the runner is absent.

- [ ] **Step 3: Implement bounded two-process GUI smoke runner**

The runner starts signaling, host, and viewer; waits for room/connection/video;
triggers drawer, mute/unmute, leave/rejoin through the GUI automation boundary;
polls all process exits; records no media paths; and finalizes its artifact
atomically. CPU/RSS samples cover a fixed stable window and fail as missing
evidence rather than fabricated zero.

- [ ] **Step 4: Run runner unit tests GREEN**

Expected: success, failure, timeout, and sanitization fixtures all pass.

- [ ] **Step 5: Build and run a native macOS GUI call**

Run host/viewer with the real signaling server and screen source. Verify local
host and remote viewer frames, microphone mute/unmute, speaker mute/unmute,
drawer, room copy feedback, leave, and rejoin. Record CPU/RSS without claiming
physical temperature or acoustic audibility.

- [ ] **Step 6: Run focused and full regression**

Run configured `call-dev` and `movie-call-dev` builds/CTest, CLI contracts,
signaled-peer 20/20, GUI contracts, smoke runner tests, Go race/vet, workflow
tests, skill validator, portable-header scan, and `git diff --check`.

- [ ] **Step 7: Fix only evidenced regressions with RED/GREEN tests**

For each issue, add the smallest test that reproduces the observed break, run
it RED, implement the correction, then rerun focused and affected full gates.

- [ ] **Step 8: Commit**

```bash
git add scripts/run_gui_call_smoke.py tests/scripts/gui_call_smoke_test.py \
  tests/scripts/CMakeLists.txt tests/scripts/rtc_demo_cli_test.py \
  client/tools/rtc_demo client/rtc/webrtc tests/rtc
git commit -m "test: gate complete GUI call lifecycle"
```

### Task 7: Verification evidence, final review, and integration

**Files:**
- Create: `docs/verification/complete-gui.md`
- Modify: `docs/development/current-stage.md`
- Modify: `README.md` only if its launch instructions would otherwise be false.

**Interfaces:**
- Produces: canonical handoff with exact code/test/runtime/platform evidence.

- [ ] **Step 1: Write verification evidence from fresh artifacts**

Record exact OS/hardware, commit, commands, test counts, native GUI action
results, CPU/RSS sample window, and limitations. Separate automated RTP/video
evidence from human acoustic/visual/physical-temperature evidence.

- [ ] **Step 2: Update canonical current stage**

Make Complete GUI the delivered stage and restore remaining Windows
screen-and-voice parity as the next product stage. Keep file sharing postponed.

- [ ] **Step 3: Perform requirement-by-requirement self-review**

Map every Included Scope and Completion Gate item in the design spec to source,
test, or an explicit unmet boundary. Any unaccounted included item blocks merge.

- [ ] **Step 4: Perform final code review**

Inspect the entire branch diff for Critical/Important lifecycle, threading,
media-quality, privacy, accessibility, and automation regressions. Fix findings
through focused RED/GREEN cycles and rerun affected gates.

- [ ] **Step 5: Run final verification from the final tree**

Freshly run the complete Task 6 gate plus documentation links and
`git diff --check`. Confirm the worktree contains no generated or unrelated
files and the external WebRTC checkout/cache state is unchanged.

- [ ] **Step 6: Commit evidence**

```bash
git add docs/verification/complete-gui.md docs/development/current-stage.md README.md
git commit -m "docs: record complete GUI acceptance"
```

- [ ] **Step 7: Push, merge, and clean only if every merge gate passes**

Push `codex/complete-gui`, fast-forward or merge it into updated `main` after
confirming no concurrent remote changes conflict, push `main`, verify both tips,
and remove the clean worktree. If a quality or environment gate is unmet, push
the feature branch and report it without merging or weakening the gate.
