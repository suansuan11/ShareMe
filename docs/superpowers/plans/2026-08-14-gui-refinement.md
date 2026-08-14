# ShareMe GUI/UX Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refine the existing ShareMe desktop GUI into a calm, coherent Chinese-first screen-sharing and voice-call product without changing RTC or media behavior.

**Architecture:** Keep `ShareMeAppController`, `RtcDemoController`, and all signaling/media ownership unchanged. Refine the existing QML hierarchy from centralized theme tokens through reusable primitives into Home, Preflight, Call, Details, dialog, recovery, and result surfaces. Add only GUI presentation mappings and deterministic GUI smoke hooks needed to test visible states.

**Tech Stack:** Qt 6 QML, Qt Quick Controls 2 Basic style, Qt Quick Layouts, Qt Multimedia `VideoOutput`, C++20 Qt controller boundary, CMake/CTest, Python GUI contract runners, macOS Cocoa/offscreen evidence.

## Global Constraints

- `ShareMeTheme.qml` is the source of truth for semantic colors, geometry, typography, and interaction tokens.
- Product language is Simplified Chinese first; technical diagnostic values remain truthful and may retain English implementation names.
- Keep one `VideoOutput`; do not add continuous decorative animations, duplicate previews, per-frame QML text updates, or independent diagnostic polling.
- Do not change signaling, PeerConnection ownership, ICE, capture backends, VideoToolbox, Media Foundation, codec selection/fallback, frame queues, bitrate policy, resolution policy, capture recovery policy, primary voice transport, movie audio, system audio, TURN, remote input, or Movie Stage 2B.
- Do not add fake microphone/speaker device selectors, fake OS-settings buttons, or controls for unsupported capabilities.
- Preserve explicit CLI validation/autostart behavior and existing GUI smoke `objectName` selectors unless a legitimate hierarchy change requires a contract update.
- Preserve the external libwebrtc checkout, build, and cache; do not clean, reconfigure, delete, or commit it.
- Use Qt Quick Layouts and anchors; do not use fixed-coordinate layout hacks that fail at compact sizes or 125/150/200% DPI and Retina scaling.
- Interactive controls have hover, pressed, disabled, visible keyboard focus, accessible names, and approximately 44 px hit areas.
- Normal interaction motion is 140–180 ms and never delays the user action.
- Do not commit screenshots, generated output, build products, logs, media, caches, secrets, local settings, or IDE state.
- Platform claims are labeled verified, partial, environment-dependent, or unimplemented with exact evidence.

---

### Task 1: Establish the Shared Visual System

**Files:**
- Modify: `client/tools/rtc_demo/qml/ShareMeTheme.qml`
- Create: `client/tools/rtc_demo/qml/IconGlyph.qml`
- Create: `client/tools/rtc_demo/qml/DialogSurface.qml`
- Modify: `client/tools/rtc_demo/qml/PrimaryButton.qml`
- Modify: `client/tools/rtc_demo/qml/IconControl.qml`
- Modify: `client/tools/rtc_demo/qml/InfoRow.qml`
- Modify: `client/tools/rtc_demo/qml/StatusBanner.qml`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Test: `tests/scripts/gui_qml_contract_test.py`

**Interfaces:**
- `ShareMeTheme` produces semantic colors, spacing, radii, typography sizes,
  control heights, drawer width, and transition duration for every page.
- `IconGlyph` consumes `name`, `size`, `color`, and optional `muted` state and
  renders the local icon set `microphone`, `speaker`, `details`, `leave`,
  `copy`, `settings`, `help`, `back`, `close`, `warning`, and `share`.
- `IconControl` consumes `iconName`, `active`, `destructive`, `enabled`, and
  `accessibleDescription`; its `clicked()` signal remains unchanged for the
  existing smoke selectors.
- `DialogSurface` provides the shared modal background, title spacing, content
  margins, close affordance, focus treatment, and `Dialog.Close` behavior used
  by Settings and Help.

- [ ] **Step 1: Add a failing visual primitive contract**

Extend `GuiQmlContractTest` with a source/module contract that requires the
new primitives to be registered:

```python
def test_shared_visual_primitives_are_registered(self):
    qml_dir = Path(__file__).parents[2] / "client" / "tools" / "rtc_demo" / "qml"
    for filename in ("IconGlyph.qml", "DialogSurface.qml"):
        self.assertTrue((qml_dir / filename).is_file())
    cmake = (qml_dir.parent / "CMakeLists.txt").read_text(encoding="utf-8")
    self.assertIn("qml/IconGlyph.qml", cmake)
    self.assertIn("qml/DialogSurface.qml", cmake)
```

Run:

```bash
cmake --build build/call-dev --target shareme_rtc_demo
ctest --test-dir build/call-dev -R '^gui_qml_contract$' --output-on-failure
```

Expected: the new test fails because the files and QML registration do not yet
exist.

- [ ] **Step 2: Define the theme tokens**

Replace the current four-color-only theme with semantic properties covering
background, surfaces, borders, text hierarchy, accent states, status colors,
spacing, radii, control heights, drawer width, and `motionFast: 160`.
Keep the existing dark direction, avoid pure black/white for large surfaces,
and use one restrained blue accent.

```qml
readonly property color background: "#0B1016"
readonly property color surface: "#121922"
readonly property color surfaceRaised: "#1A2430"
readonly property color border: "#2A3542"
readonly property color textPrimary: "#EDF2F7"
readonly property color textSecondary: "#A8B4C0"
readonly property color textMuted: "#71808F"
readonly property color accent: "#6EA8E8"
readonly property color success: "#72D5B0"
readonly property color warning: "#E6C477"
readonly property color error: "#E07A86"
readonly property int controlHeight: 44
readonly property int drawerWidth: 320
readonly property int motionFast: 160
```

- [ ] **Step 3: Implement deterministic local icons and shared surfaces**

Implement `IconGlyph.qml` with a small `Canvas` path table for the named icons;
draw only static vectors and call `requestPaint()` when `name`, `color`, or
`size` changes. Implement `DialogSurface.qml` as a styled `Dialog` wrapper
with a shared background and focus-visible close button.

Update `IconControl.qml` to use `IconGlyph`, preserve `implicitWidth` and
`implicitHeight` at least 44 px, expose tooltip/accessibility text, and style
hover/pressed/active/disabled/focus states. Update `PrimaryButton`, `InfoRow`,
and `StatusBanner` to consume semantic tokens rather than inline colors.

- [ ] **Step 4: Run the primitive contract and offscreen load**

Run:

```bash
cmake --build build/call-dev --target shareme_rtc_demo
ctest --test-dir build/call-dev -R '^(gui_qml_contract|gui_call_smoke_contract)$' --output-on-failure
```

Expected: both tests pass with no QML `TypeError`, `ReferenceError`, binding
loop, or component-load warning.

- [ ] **Step 5: Commit the shared system**

```bash
git add client/tools/rtc_demo/qml client/tools/rtc_demo/CMakeLists.txt \
  tests/scripts/gui_qml_contract_test.py
git commit -m "style: establish ShareMe visual system"
```

### Task 2: Refine Home and Create/Join Preflight

**Files:**
- Modify: `client/tools/rtc_demo/qml/HomePage.qml`
- Modify: `client/tools/rtc_demo/qml/PreflightPage.qml`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `tests/scripts/gui_qml_contract_test.py`
- Modify: `scripts/run_gui_call_smoke.py`
- Modify: `tests/scripts/gui_call_smoke_test.py`

**Interfaces:**
- Preserve `HomePage` signals `openSettings()` and `openHelp()` and
  `ShareMeAppController.showCreateRoom()`, `showJoinRoom()`,
  `joinRecentRoom()`, and `forgetRecentRoom()`.
- Preserve `PreflightPage` bindings to `roomCode`, `screenProfile`,
  `microphoneEnabled`, `speakerEnabled`, `errorMessage`, and `startCall()`.
- Produce stable object names `createRoomButton`, `joinRoomButton`,
  `recentRoomAction`, `roomCodeField`, `microphoneIntentControl`,
  `speakerIntentControl`, `qualityProfileControl`, and
  `preflightPrimaryButton` for GUI contracts.

- [ ] **Step 1: Add failing Home/Preflight state contracts**

Extend the GUI contract to assert the new state markers and required object
names in the real QML tree. Keep the existing home/create/join smoke states and
add assertions for user-facing strings:

```python
def test_home_and_preflight_expose_primary_actions(self):
    for state, required in (
        ("home", ("createRoomButton", "joinRoomButton")),
        ("create", ("preflightPrimaryButton", "qualityProfileControl")),
        ("join", ("roomCodeField", "preflightPrimaryButton")),
    ):
        result = self.run_state(state)
        self.assertEqual(result.returncode, 0, result.stderr)
        for object_name in required:
            self.assertIn(f"GUI_OBJECT {object_name}=1", result.stdout)
```

Update the smoke hook in `main.cpp` only as a GUI test seam to print those
object markers; it must not construct a session or change controller state.
Run the test before implementation and expect failure because the names and
markers do not exist.

- [ ] **Step 2: Replace the Home composition**

Remove the decorative gradient preview rectangle and build a responsive
top-bar-plus-hero layout. Keep the hero within a bounded width, make `创建房间`
the primary filled button, make `加入房间` the quiet secondary button, and
place the single recent-room row below them. Use the shared icon/dialog
primitives for Settings and Help and keep all recent-room actions readable in
the Basic style.

```qml
PrimaryButton {
    objectName: "createRoomButton"
    text: "创建房间"
    onClicked: page.appController.showCreateRoom()
}
PrimaryButton {
    objectName: "joinRoomButton"
    text: "加入房间"
    secondary: true
    onClicked: page.appController.showJoinRoom()
}
```

Do not add diagnostics, capabilities, accounts, or unsupported claims to Home.

- [ ] **Step 3: Replace the Preflight composition**

Use one centered surface with sections `房间`, `设备`, and `共享质量`. In
join mode, put `roomCodeField` first, show the existing validation banner, and
disable `preflightPrimaryButton` until the normalized room code is valid. In
create mode, explain that the current screen will be shared and keep the
quality selector enabled.

Use existing microphone/speaker intent switches, but label them as current
intent rather than device selection. Use the three user-facing profile strings
without changing the `standard`, `quality`, or `cinema` property values sent to
C++.

- [ ] **Step 4: Implement compact layout and keyboard order**

Use `ColumnLayout` at compact widths, keep the primary action visible without
scrolling at the frozen minimum window size, and set explicit `objectName` and
`Accessible.name` values for the room field, audio intents, quality selector,
back action, and primary action. Add a visible focus border from the shared
theme.

- [ ] **Step 5: Run Home/Preflight contracts and the focused smoke runner**

Run:

```bash
cmake --build build/call-dev --target shareme_rtc_demo
ctest --test-dir build/call-dev -R '^(gui_qml_contract|gui_call_smoke_contract)$' --output-on-failure
```

Expected: home/create/join load cleanly, the new object markers are present,
the recent-room flow remains bounded to one room, and no QML warning appears.

- [ ] **Step 6: Commit Home and Preflight**

```bash
git add client/tools/rtc_demo/qml/Main.qml \
  client/tools/rtc_demo/qml/HomePage.qml \
  client/tools/rtc_demo/qml/PreflightPage.qml \
  client/tools/rtc_demo/main.cpp \
  tests/scripts/gui_qml_contract_test.py \
  scripts/run_gui_call_smoke.py tests/scripts/gui_call_smoke_test.py
git commit -m "style: refine home and preflight experience"
```

### Task 3: Refine the Active Call Stage and Controls

**Files:**
- Modify: `client/tools/rtc_demo/qml/CallPage.qml`
- Modify: `client/tools/rtc_demo/qml/CallTopBar.qml`
- Modify: `client/tools/rtc_demo/qml/VideoStage.qml`
- Modify: `client/tools/rtc_demo/qml/CallControlDock.qml`
- Modify: `client/tools/rtc_demo/qml/CallDetailsDrawer.qml`
- Modify: `tests/scripts/gui_qml_contract_test.py`

**Interfaces:**
- Preserve `CallPage` properties `appController`, `controller`, and
  `detailsOpen`, plus `onToggleDetails` and `onLeaveRequested` behavior.
- Preserve object names `callPage`, `microphoneControl`, `speakerControl`,
  `detailsControl`, `leaveControl`, and `speakerVolumeControl`.
- Consume only existing controller properties and invokables, including
  `status`, `roomId`, `viewer`, `remoteVideoAvailable`, `microphoneMuted`,
  `speakerMuted`, `setMicrophoneMuted`, `setSpeakerMuted`, and `setVideoSink`.

- [ ] **Step 1: Add failing call-stage contracts**

Extend `GuiQmlContractTest` so host and viewer call states assert:

```python
self.assertIn("GUI_OBJECT callPage=1", result.stdout)
self.assertIn("GUI_OBJECT microphoneControl=1", result.stdout)
self.assertIn("GUI_OBJECT speakerControl=1", result.stdout)
self.assertIn("GUI_OBJECT detailsControl=1", result.stdout)
self.assertIn("GUI_OBJECT leaveControl=1", result.stdout)
self.assertIn("GUI_OBJECT shareControl=0", result.stdout)
```

The last assertion captures removal of the non-operational share button. Run
the focused test and expect failure before the dock is changed.

- [ ] **Step 2: Make the stage own the available space**

Change `CallPage.qml` from a full-width bottom layout dock to a top bar plus a
stage item. Anchor `VideoStage` between the top bar and the control overlay;
keep exactly one `VideoOutput`. Anchor the normal dock at the stage bottom
center, and anchor the regular drawer over the right side without permanently
shrinking the stage. At compact width, switch the drawer to a bottom sheet and
keep the dock above it.

- [ ] **Step 3: Simplify the top bar and status copy**

Keep the room-code copy action and `roomCopied` signal. Reduce the bar to
ShareMe, formatted room code with a copy icon, and a small connection state.
Map raw statuses such as `connected`, `negotiating`,
`screen-capture-recovering:*`, and `session-suspended:*` to calm Chinese copy
inside the presentation layer. Do not display codec, bitrate, FPS, encoder,
generation, or packet values.

- [ ] **Step 4: Simplify the VideoStage states**

Remove the decorative stage border and large placeholder card. Keep
`VideoOutput.fillMode: VideoOutput.PreserveAspectFit`, a neutral letterbox, and
small status text. Use `正在等待屏幕共享…` for a viewer without remote video and
`正在准备屏幕共享…` for a host before connection. Keep
`正在恢复屏幕共享` and the existing voice-continuity explanation for capture
recovery. Do not add a timer, frame copy, thumbnail, or shader.

- [ ] **Step 5: Rebuild the dock with icon controls**

Replace `symbol` values with the shared `IconGlyph` names. Keep the microphone
and speaker calls exactly as they are, including state updates only after the
controller operation succeeds. Keep details toggle and leave routing. Remove
the disabled share control from the visual tree so it cannot be mistaken for a
working operation. Give each icon an accessible Chinese description and
tooltip.

- [ ] **Step 6: Run host/viewer call contracts and action probes**

Run:

```bash
cmake --build build/call-dev --target shareme_rtc_demo
ctest --test-dir build/call-dev -R '^(gui_qml_contract|gui_call_smoke_contract)$' --output-on-failure
```

Expected: host and viewer call pages load without QML warnings, the stage has
one video output, the four controls remain discoverable, the smoke action still
toggles microphone and speaker, opens details, rejects an unsupported volume
request without changing state, leaves, and returns Home.

- [ ] **Step 7: Commit the active call refinement**

```bash
git add client/tools/rtc_demo/qml/CallPage.qml \
  client/tools/rtc_demo/qml/CallTopBar.qml \
  client/tools/rtc_demo/qml/VideoStage.qml \
  client/tools/rtc_demo/qml/CallControlDock.qml \
  client/tools/rtc_demo/qml/CallDetailsDrawer.qml \
  tests/scripts/gui_qml_contract_test.py
git commit -m "style: refine active call interface"
```

### Task 4: Refine Details, Settings, Help, and Recovery

**Files:**
- Modify: `client/tools/rtc_demo/qml/CallDetailsDrawer.qml`
- Modify: `client/tools/rtc_demo/qml/SettingsDialog.qml`
- Modify: `client/tools/rtc_demo/qml/HelpDialog.qml`
- Modify: `client/tools/rtc_demo/qml/RecoveryDialog.qml`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `tests/scripts/gui_qml_contract_test.py`
- Modify: `scripts/run_gui_call_smoke.py`
- Modify: `scripts/run_windows_gui_acceptance.py`
- Modify: `tests/scripts/windows_gui_acceptance_test.py`

**Interfaces:**
- Preserve all existing controller diagnostic properties and movie playback
  invokables; only their grouping and copy changes.
- Preserve `SettingsDialog` binding to `serverUrl` and `serverUrlChanged`.
- Preserve `RecoveryDialog` calls to `returnHome()` and `retryCall()`.
- Produce semantic `connectionSection`, `videoSection`, `audioSection`,
  `advancedSection`, `settingsDialog`, `helpDialog`, and `recoverySurface`
  object names for GUI contracts.

- [ ] **Step 1: Add failing surface coverage**

Add deterministic GUI smoke states `settings`, `help`, and `recovery`. The
smoke hook may invoke dialog `open()` or a QML-only recovery preview property,
but it must not create a media session or change controller state. Extend the
Python contract to assert:

```python
for state, object_name in (
    ("settings", "settingsDialog"),
    ("help", "helpDialog"),
    ("recovery", "recoverySurface"),
):
    result = self.run_state(state)
    self.assertEqual(result.returncode, 0, result.stderr)
    self.assertIn(f"GUI_OBJECT {object_name}=1", result.stdout)
```

Add these states to `run_gui_call_smoke.py` and the Windows probe list. Run the
focused contract and expect failure before the new smoke states exist.

- [ ] **Step 2: Group the details drawer**

Replace the current flat rows with `连接`, `画面`, `声音`, optional `播放`, and
closed `高级信息` sections. Keep the microphone level, speaker-volume control,
voice quality message, and all existing movie controls. Move codec,
implementation, hardware, presentation, audio-route, drift, and scheduler
values into the advanced section. Remove raw `controller.status` display from
the normal section and map it to user-facing copy.

- [ ] **Step 3: Apply the shared dialog surface**

Make Settings and Help use `DialogSurface`. Settings has one concise
`连接地址` field with helper copy explaining that it is a development
connection setting and is not persisted with room data. Help uses short
question/answer groups for create, join, missing video, missing voice, and
macOS screen-recording permission, plus compact keyboard guidance.

- [ ] **Step 4: Redesign recovery copy and actions**

Keep temporary `screen-capture-recovering:*` and session lifecycle states in the
call. For terminal results, map the existing sanitized categories to titles:

```qml
permission-denied -> "需要检查权限"
invalid-room      -> "无法加入这个房间"
screen/capture    -> "屏幕共享不可用"
audio/device      -> "声音设备不可用"
connection/ICE    -> "连接未建立"
otherwise         -> "通话暂时无法继续"
```

Use the existing friendly `errorMessage`, `重试`, and `返回首页` actions. Remove
the visible `诊断类别：...` row from normal recovery and do not add a fake
system-settings button.

- [ ] **Step 5: Run surface contracts and action smoke**

Run:

```bash
cmake --build build/call-dev --target shareme_rtc_demo
ctest --test-dir build/call-dev -R '^(gui_qml_contract|gui_call_smoke_contract|windows_gui_acceptance)$' --output-on-failure
```

If `windows_gui_acceptance` is not registered in the current macOS configure,
run the two Python contract tests directly and record Windows as
environment-dependent. Expected result: details default closed, advanced
diagnostics expand without warnings, dialogs load, recovery copy contains no
raw implementation error, and all existing action selectors still work.

- [ ] **Step 6: Commit secondary surfaces**

```bash
git add client/tools/rtc_demo/qml \
  client/tools/rtc_demo/main.cpp \
  tests/scripts/gui_qml_contract_test.py \
  scripts/run_gui_call_smoke.py scripts/run_windows_gui_acceptance.py \
  tests/scripts/windows_gui_acceptance_test.py
git commit -m "style: refine details and recovery surfaces"
```

### Task 5: Strengthen GUI Contracts and Regression Coverage

**Files:**
- Modify: `tests/scripts/gui_qml_contract_test.py`
- Modify: `tests/scripts/gui_call_smoke_test.py`
- Modify: `scripts/run_gui_call_smoke.py`
- Modify: `scripts/run_windows_gui_acceptance.py`
- Modify: `tests/scripts/windows_gui_acceptance_test.py`
- Modify: `tests/scripts/CMakeLists.txt`
- Test: existing `tests/rtc/shareme_app_controller_test.cpp`
- Test: existing `tests/rtc/rtc_demo_control_state_test.cpp`
- Test: existing `tests/rtc/app_session_config_test.cpp`

**Interfaces:**
- Preserve the existing machine-readable markers `GUI_STATE` and
  `GUI_ACTION`.
- Extend markers with `GUI_OBJECT name=0|1` and keep the output sanitized.
- Keep the existing six core probes and add settings/help/recovery surfaces
  without weakening early-exit, timeout, or warning detection.

- [ ] **Step 1: Add red tests for sanitized GUI behavior**

Add assertions that normal GUI output contains no raw `kVTParameterErr`,
`HRESULT`, `NSError`, `ICE`, `SDP`, absolute path, or credential string, while
the details test still confirms technical values remain available under the
advanced section. Add a compact-layout probe that exits cleanly at the frozen
minimum size.

- [ ] **Step 2: Update smoke runners atomically**

Extend the probe tuple in `run_gui_call_smoke.py` to include `settings`, `help`,
and `recovery`, update the expected probe count in `GUI_SMOKE`, and retain the
existing partial-result and artifact-atomicity tests. Extend the Windows
surface list/probe tuple in `run_windows_gui_acceptance.py` while preserving its
DPI scale checklist and environment-dependent status.

- [ ] **Step 3: Run focused Python contracts**

Run:

```bash
python3 -m unittest tests/scripts/gui_call_smoke_test.py
python3 -m unittest tests/scripts/windows_gui_acceptance_test.py
```

Expected: all fake-runner behavior passes, including early exit, timeout,
partial results, atomic artifact replacement, sanitization, and DPI checklist.

- [ ] **Step 4: Run controller and CLI regression tests**

Run:

```bash
ctest --test-dir build/call-dev -R '^(app_controller|app_preferences|app_session_config|rtc_demo_control_state|launch_options|rtc_demo_cli_contract|signaled_peer)$' --output-on-failure
```

Expected: navigation, room normalization, mute state, idempotent leave,
explicit CLI validation, and signaled-peer behavior remain unchanged.

- [ ] **Step 5: Commit GUI contract updates**

```bash
git add tests/scripts scripts/run_gui_call_smoke.py \
  scripts/run_windows_gui_acceptance.py tests/scripts/CMakeLists.txt
git commit -m "test: update GUI acceptance contracts"
```

### Task 6: Fresh Verification, Manual Review, and Evidence

**Files:**
- Modify: `docs/verification/complete-gui.md`
- Modify: `docs/development/current-stage.md`
- Create: `docs/verification/gui-refinement.md`
- Do not commit: temporary screenshots, smoke JSON, build output, or local settings.

**Interfaces:**
- Produce a final verification document with exact branch SHA, commands,
  automated counts, manual states, performance samples, and platform boundaries.
- Preserve the existing complete-GUI media evidence and explicitly state that
  this GUI stage did not change RTC/media behavior.

- [ ] **Step 1: Configure fresh call and movie-call builds**

From the isolated worktree, run:

```bash
cmake --preset call-dev
cmake --build --preset build-call-dev -j2
cmake --preset movie-call-dev
cmake --build --preset build-movie-call-dev -j2
```

Do not touch the repository-external WebRTC cache. Record configure/build
failures as environment-dependent rather than changing media configuration.

- [ ] **Step 2: Run focused and full automated suites**

Run:

```bash
ctest --test-dir build/call-dev --output-on-failure
ctest --test-dir build/movie-call-dev --output-on-failure
python3 -m unittest tests/scripts/gui_qml_contract_test.py
python3 -m unittest tests/scripts/gui_call_smoke_test.py
git diff --check
```

Run the repository's existing Go race/vet and workflow/skill checks when their
toolchain is available. Record exact pass counts from command output.

- [ ] **Step 3: Capture manual macOS visual states**

Launch the fresh `shareme_rtc_demo` and inspect Home, create preflight, join
preflight, active host call, active viewer call, details open, Settings, Help,
temporary recovery, and terminal error. Inspect compact minimum, normal laptop,
and large desktop sizes. Exercise Tab/Enter/Space/Escape, hover, pressed,
disabled, mute, speaker, details, leave, copy, retry, and return-home actions.
Store screenshots only under `/var/folders/.../T/opencode` or another ignored
temporary location and do not stage them.

- [ ] **Step 4: Measure GUI idle performance**

Run the existing smoke runner against the fresh executable with a temporary
artifact path:

```bash
python3 scripts/run_gui_call_smoke.py \
  --demo build/call-dev/client/tools/rtc_demo/shareme_rtc_demo \
  --artifact /var/folders/zf/yfwlct4x0yxdnf334_1cd13h0000gn/T/opencode/gui-refinement-smoke.json \
  --idle-sample-seconds 3
```

Compare the fresh macOS offscreen CPU/RSS summary with the baseline 4.95% mean
CPU and 83,440 KiB maximum RSS. Report a modest change as acceptable and
investigate any major unexplained increase before completion.

- [ ] **Step 5: Write verification evidence and update the handoff**

In `docs/verification/gui-refinement.md`, record:

- starting and final SHAs;
- changed pages/components;
- design-system and UX outcomes;
- exact automated test counts;
- manual states inspected and macOS result;
- Windows result as environment-dependent unless Windows execution exists;
- idle CPU/RSS before/after;
- explicit statement that no RTC/media behavior changed;
- known limitations and parked device-selection opportunity.

Update `docs/verification/complete-gui.md` only for links or superseded GUI
presentation evidence, and update `docs/development/current-stage.md` at the
stage boundary with exact evidence labels. Do not rewrite historical media
claims.

- [ ] **Step 6: Perform final Git and architecture review**

Run:

```bash
git status --short --branch
git diff --stat main...HEAD
git diff --check main...HEAD
git log --oneline --decorate -12
```

Inspect the complete branch diff for broken navigation, stale selectors,
text clipping, compact-layout overlap, keyboard-focus loss, hidden primary
actions, raw technical copy, media/controller changes, and generated files.

- [ ] **Step 7: Commit evidence**

```bash
git add docs/verification/gui-refinement.md \
  docs/verification/complete-gui.md docs/development/current-stage.md
git commit -m "docs: record GUI refinement verification"
```

## Plan Self-Review

- Visual tokens and reusable primitives are covered by Task 1.
- Immediate Create/Join hierarchy, Chinese copy, recent-room behavior, and
  truthful preflight capabilities are covered by Task 2.
- Dominant shared stage, low-profile top bar, four essential controls, waiting
  state, recovery indicator, compact layout, and no extra video path are covered
  by Task 3.
- Semantic diagnostics, advanced disclosure, Settings, Help, sanitized errors,
  and recovery actions are covered by Task 4.
- Accessibility selectors, compact smoke, GUI state contracts, CLI/controller
  regressions, and artifact handling are covered by Task 5.
- Fresh build, full CTest, manual visual review, performance comparison, Git
  hygiene, and macOS/Windows evidence boundaries are covered by Task 6.
- No task changes RTC/media architecture or introduces unsupported device
  selection.
