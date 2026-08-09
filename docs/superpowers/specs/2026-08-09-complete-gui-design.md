# ShareMe Complete GUI Design

## Purpose

Replace the engineering-oriented single-screen RTC demo UI with a complete,
coherent application experience for high-quality screen-sharing calls. The
approved visual direction is **Calm Dark**: an immersive near-black stage,
blue/cyan status accents, restrained surfaces, and strong contrast for primary
actions. The approved call layout is **Focus Stage plus On-demand Drawer**:
shared content owns the window while participants, quality, device state, and
diagnostics remain one action away.

The GUI stage takes priority before the next media stage. After it is accepted,
development returns to Windows screen-and-voice parity, device selection, and
physical two-device acceptance. File sharing remains postponed.

## Product principles

1. **Content first.** The shared screen is the visual center of every active
   call and is never permanently compressed by diagnostic information.
2. **One-step call controls.** Microphone mute, speaker mute, screen sharing,
   settings, room-code copy, and leave are visible and keyboard reachable.
3. **Progressive disclosure.** Friendly state and recovery guidance are shown
   by default; codec, timing, queue, and transport detail lives under Advanced
   Diagnostics.
4. **Truthful capability.** A control is enabled only when the underlying
   operation is implemented for the current role, source, build, and platform.
   No visual toggle may imply an unsupported device or capture operation.
5. **Automation compatibility.** Existing command-line validation and smoke
   runners retain deterministic autostart behavior and machine-readable output.
6. **Platform honesty.** Portable QML behavior may be verified on macOS, but
   Windows native capture, audio, permission, and device behavior keeps its own
   evidence boundary.

## Frozen scope

### Included in the GUI stage

- a polished launch page with Create Room, Join Room, Settings, Help, and a
  bounded recent-room convenience entry;
- pre-call setup for room code, role, microphone/playout intent, available
  screen-quality profiles, and a concise capability/permission check;
- an in-call screen with connection state, room code and copy action, focused
  video stage, collapsible details drawer, and bottom control dock;
- working microphone mute/unmute and speaker mute/unmute controls backed by the
  primary WebRTC audio track/playout path;
- a truthful screen-share control: active host screen calls may pause/resume
  presentation only if the capture path supports it in this stage; otherwise
  the control reports the supported state instead of pretending to stop OS
  capture;
- explicit leave-call behavior that shuts down the current session and returns
  to the launch page without terminating the application;
- connection, waiting, empty-video, invalid-room, permission/capture failure,
  device/route failure, reconnecting, peer-left, and generic recoverable/fatal
  states;
- responsive layouts for compact and regular desktop windows;
- keyboard focus, labels/tooltips, readable contrast, scalable typography,
  reduced-motion-safe transitions, and non-color-only status cues;
- migration of existing movie playback controls and technical diagnostics into
  role-aware panels without changing movie synchronization behavior;
- QML component tests/static contracts, controller state-machine tests, CLI
  compatibility tests, and native GUI smoke evidence on macOS.

### Deferred after the GUI stage

- arbitrary live audio-device switching where the current WebRTC ADM cannot
  safely switch devices in-place;
- a native display/window picker until the capture backends expose an audited
  enumeration/selection contract;
- Windows hardware encoding and remaining quality/cinema cadence work;
- Windows physical two-device voice and visual acceptance;
- macOS and Windows system-audio capture;
- authentication, contact lists, persistent cloud account state, chat, remote
  input, TURN/public-network certification, HDR, and 4K60 optimization;
- file sharing and postponed movie hard-resync work.

Deferred items may appear only as clearly labeled unavailable information in
an Advanced section; they must not appear as active controls.

## Information architecture

The application has four top-level presentation states owned by a new
application/session coordinator:

1. `home` — no active RTC session;
2. `preflight` — create/join configuration is being validated;
3. `calling` — a controller exists and the signaling/media session is active;
4. `result` — a recoverable or terminal outcome is explained before returning
   home or retrying.

The QML root uses a `StackView` or equivalent explicit state host. It does not
infer navigation from raw status strings. Modal dialogs and toasts are
secondary overlays and never become hidden navigation state.

### Home

The home view contains:

- ShareMe wordmark and a short screen-call value statement;
- primary Create Room action;
- secondary Join Room action;
- Settings and Help affordances;
- at most one locally remembered, non-sensitive recent room code with a clear
  action to forget it.

The signaling URL is not a prominent product field. Development builds may
expose it under Advanced Settings; production/default behavior uses the
configured local or packaged endpoint. No credentials are persisted.

### Preflight

Create mode defaults to host, native screen source, microphone enabled, native
speaker playout enabled, and the `standard` profile. Join mode accepts and
normalizes the room code, defaults to viewer, microphone enabled, and speaker
playout enabled.

The preflight view shows only choices that are real for the current build:

- room code for join mode;
- microphone on/off intent;
- speaker on/off intent;
- screen profile for a host screen call;
- a capture/audio readiness summary;
- a disabled explanation for platform/build capabilities that are not present.

Movie and synthetic/test-source modes remain CLI/developer flows in this stage;
their existing in-call controls remain supported when the binary is launched
with those arguments.

### In-call focus stage

The top bar contains application identity, a textual connection-quality state,
and a room-code pill with copy affordance. The main stage preserves aspect
ratio and displays the local host track or remote viewer track according to the
existing controller contract.

The bottom dock contains, from left to right:

- microphone mute/unmute;
- speaker mute/unmute;
- screen-share status/action for a host, or remote-share status for a viewer;
- settings/details drawer toggle;
- leave call.

Controls show icon, accessible name, tooltip, selected/muted state, and a short
text label when window width permits. Destructive leave uses red and requires
confirmation only when the host is ending an established room; leaving while
connecting or as a viewer remains immediate and recoverable.

The right drawer is closed by default and contains:

- participant/role summary;
- selected quality profile and observed capture geometry;
- microphone, speaker, and route state;
- connection and media health;
- movie controls when available;
- an Advanced Diagnostics disclosure containing the existing codec, hardware,
  presentation, drift, renderer, and scheduler values.

### Result and recovery

Errors are mapped from typed controller/session outcomes to user language and
an allowed recovery action. Examples:

- invalid/expired room: edit code or return home;
- signaling unavailable: retry or edit the development server endpoint;
- microphone unavailable: continue muted only when the peer can be created
  truthfully without the microphone, otherwise open system guidance;
- screen permission/capture failure: remain in the call with voice when the
  media contract permits, or return to preflight;
- route/device failure: show current safe audio state and retry the supported
  route operation;
- peer left: keep the host room waiting or let the viewer return home;
- fatal RTC initialization failure: preserve a sanitized diagnostic category
  and return home.

Raw SDP, ICE candidates, paths, credentials, and unsanitized platform error
text are never shown or persisted by the GUI.

## Architecture

### Application coordinator

Introduce an application-level Qt object, tentatively `ShareMeAppController`,
that owns navigation state, validated preflight configuration, the active
`RtcDemoController`, and user-safe outcomes. QML receives one stable root
object instead of requiring an already-constructed RTC controller.

Responsibilities:

- expose typed page/session state and capability flags;
- validate and normalize room/server/profile inputs;
- construct one `RtcDemoController` for the accepted configuration;
- connect controller status/failure/room/video signals to presentation state;
- stop and destroy the RTC controller before returning home;
- expose a `videoController` QObject pointer for the in-call page;
- preserve CLI autostart by accepting a prevalidated startup configuration;
- keep local UI preferences bounded and non-sensitive.

The coordinator does not reimplement signaling or media. `RtcDemoController`
continues to own one RTC session and its existing movie/screen integration.

### Session configuration

Replace scattered UI-facing booleans with a typed configuration value carrying:

- server URL;
- host/viewer role;
- requested room;
- screen/test/movie source selected by the existing CLI boundary;
- screen profile;
- microphone/synthetic audio mode;
- native playout intent;
- existing movie and measurement-only fields.

CLI parsing still fails closed before UI startup for automation-only invalid
combinations. Interactive mode starts when role/server are not supplied and
uses the coordinator's preflight validation. A valid explicit role/server
combination retains current automatic call startup for smoke runners.

### Audio controls

Extend `SignaledPeer` with narrow, thread-safe primary-audio operations:

- enable/disable its local voice audio track;
- enable/disable remote primary voice playout/track state;
- return success/current state without rebuilding the peer.

`RtcDemoController` exposes these as role-independent microphone and speaker
properties/invokables. State changes are performed on the appropriate WebRTC
thread or through the existing peer synchronization boundary. Failure leaves
the previous state intact and produces a typed, user-safe status.

Movie audio remains isolated. The speaker control for a movie viewer must label
whether it controls primary voice only; movie audio cannot be accidentally
muted through an unrelated primary-voice toggle.

### Leave and lifecycle

`RtcDemoController::stop()` becomes an idempotent public operation on the Qt
owning thread. It stops timers and ingress, cancels waits, detaches media sinks,
stops peers/signaling, and emits a terminal session state before deletion.
The coordinator waits for this boundary before discarding the controller.

Late callbacks remain guarded by the existing shutdown mechanisms. No QML page
may retain a stale controller pointer after the coordinator transitions home.

### QML component system

Split the monolithic `Main.qml` into small components under the existing QML
module:

- `Main.qml` — application window, theme, navigation, global overlays;
- `HomePage.qml`;
- `PreflightPage.qml`;
- `CallPage.qml`;
- `CallTopBar.qml`;
- `CallControlDock.qml`;
- `CallDetailsDrawer.qml`;
- `VideoStage.qml`;
- `StatusBanner.qml`;
- `RecoveryDialog.qml`;
- reusable `PrimaryButton`, `IconControl`, `InfoRow`, and theme singleton.

Icons must be repository-native vector paths, text glyphs with deterministic
fallback, or Qt-drawn primitives. Generated raster assets and external icon
downloads are excluded.

### Responsive behavior

- regular width: video stage plus overlay/on-demand right drawer;
- compact width: drawer becomes a bottom sheet so the stage retains a useful
  minimum width;
- labels collapse before controls; icons never lose accessible names;
- window minimum size protects the control dock from overlap;
- video always uses preserve-aspect-fit and a neutral black letterbox.

## Visual system

The frozen palette uses near-black `#090D14` for the application, elevated
`#101720`/`#18222E` surfaces, white-blue primary text, muted slate secondary
text, blue `#1686D9` primary actions, cyan status accents, green healthy state,
amber warning, and red only for destructive/error actions.

Spacing follows a small 4/8/12/16/24 scale. Corners are restrained: 8 px for
inputs, 12 px for media surfaces, and 16–20 px for sheets/modals. Motion is
limited to 120–200 ms opacity/position transitions and is disabled or reduced
when the platform requests reduced motion where Qt exposes that preference.

## State and data rules

- Room codes are trimmed, uppercased, and validated through the existing
  protocol constraints before a session starts.
- UI state uses enums/booleans and typed error categories, not comparisons
  against presentation strings.
- `connected` means signaling/peer establishment according to the existing
  controller contract; it does not claim visible frames or audible sound.
- Video readiness, microphone state, speaker state, and capture state are
  separate indicators.
- Diagnostic values may be absent. The UI renders `Unavailable` rather than
  fabricating zero, healthy, hardware, or synchronized results.
- The details drawer observes controller snapshots and performs no polling or
  blocking media operation from QML.

## GUI performance budget

The new shell must not undermine the existing thermal/performance work:

- no decorative animation runs continuously during a call;
- status animations stop when their state is stable and honor window
  visibility;
- collapsed drawers and hidden diagnostic sections do not instantiate costly
  delegates or add independent polling timers;
- video remains a single `VideoOutput`; thumbnails and duplicate live previews
  are not created for decoration;
- QML bindings consume the controller's existing bounded snapshots rather than
  performing per-frame text/layout updates;
- GUI smoke records idle and active-call process CPU/RSS samples as regression
  evidence, but does not replace the established media quality gates or claim
  physical temperature without measurement.

## Testing strategy

Implementation follows test-first slices.

### Portable/controller tests

- interactive versus explicit CLI startup classification;
- preflight validation and room-code normalization;
- coordinator transitions: home -> preflight -> calling -> result/home;
- one active controller only and idempotent leave;
- local microphone and remote speaker toggles, including failed operations;
- late callback safety across leave/rejoin;
- typed error-to-recovery mapping;
- capability-based visibility/enabling rules.

### QML/UI contracts

- all required QML files are registered and load without warnings;
- accessible names and keyboard focus exist for primary actions;
- home, preflight, calling, waiting, drawer, and error states render offscreen;
- compact and regular layouts do not overlap at frozen minimum sizes;
- diagnostics are hidden by default and show truthful unavailable states;
- host/viewer and screen/movie roles expose only valid controls.

Where reliable image comparison is available, small deterministic screenshots
may be kept as ignored test artifacts; generated screenshots are not committed.

### Compatibility and native evidence

- existing `--validate` combinations and CLI tests remain green;
- existing screen and movie smoke runners keep explicit autostart behavior;
- `call-dev` and `movie-call-dev` configure/build and their CTest suites pass;
- macOS native host/viewer confirms create/join, room copy, local/remote video,
  microphone mute, speaker mute, drawer, leave, and rejoin;
- a bounded manual pass checks readability, focus traversal, compact resizing,
  and no QML warnings.

Windows compilation/static portability is required before merge when the
available Windows environment can run it. Native Windows UI/capture/audio and
physical display evidence remains environment-dependent unless actually run.

## Completion and merge gate

The GUI stage is mergeable only when:

- the four approved application states are implemented with Calm Dark styling;
- core controls are wired to real controller/peer behavior or explicitly
  unavailable with a truthful reason;
- command-line automation remains compatible;
- focused controller and QML contracts pass;
- configured full test suites and relevant smoke runners pass on the executing
  platform;
- a native two-process GUI call completes create/join, media, toggles,
  leave/rejoin, and recovery checks;
- the worktree contains no generated files, screenshots, logs, media, caches,
  secrets, or unrelated changes;
- final review has no Critical or Important finding;
- `current-stage.md` and a dedicated GUI verification document record exact
  proof and environment-dependent boundaries.

After this gate, merge and push the focused GUI branch, clean its worktree, and
resume the remaining Windows high-quality screen-and-voice stage. No claim of
Windows native UI acceptance is permitted without Windows execution evidence.
