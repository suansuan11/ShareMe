# ShareMe GUI/UX Refinement Design

## Status

Approved product direction for the GUI refinement stage.

- Starting `main` SHA: `98b93d333925bc8b483cb6130330ccdf70ba77b5`
- Feature branch: `codex/gui-refinement`
- Product language: Simplified Chinese first; technical diagnostic values stay truthful and may retain English implementation names.
- Primary approach: focused product-shell refinement over the existing application/controller contracts.

## Purpose

Refine the existing functional ShareMe desktop GUI into a calm, coherent,
desktop-native screen-sharing and voice-call product. The application should
make the core flow immediately obvious:

`打开 ShareMe -> 创建或加入房间 -> 准备声音和画面 -> 共享并交流`

This stage improves hierarchy, copy, spacing, typography, interaction states,
responsive behavior, and progressive disclosure. It does not redesign the RTC
or media architecture.

## Audit Summary

### Strengths to preserve

- `ShareMeAppController` already owns application navigation and one active session.
- `RtcDemoController` exposes real microphone, speaker, voice-quality, video, recovery, and diagnostic state.
- Create/join, recent-room privacy, leave, retry, and return-home behavior are already wired.
- The call page has one `VideoOutput`, preserve-aspect presentation, a compact details path, and a working offscreen GUI contract.
- Explicit CLI startup remains separate from no-argument interactive startup.

### Problems to address

- The Home page gives a decorative gradient panel nearly equal weight to Create and Join.
- Recent-room actions are visually weak under the current Basic style.
- Preflight behaves like an asymmetric settings page instead of a short preparation step.
- Color, radius, and spacing values are repeated across QML files.
- Production controls use inconsistent text glyphs instead of one icon language.
- The call dock includes a disabled share control that adds noise without an operation.
- Normal call state and developer diagnostics are mixed together in the drawer.
- Raw enum/status terminology and technical error categories are too visible.
- Settings, Help, and Recovery do not share a consistent surface and action hierarchy.

## Scope

### Included

- Shared QML design tokens and reusable visual primitives.
- Home, create preflight, join preflight, active call, details, settings, help,
  recovery, and error-state presentation.
- User-facing Chinese copy, status mapping, labels, and accessibility text.
- Layout, typography, spacing, radii, iconography, hover/pressed/focus/disabled
  states, keyboard traversal, and short functional transitions.
- GUI-only controller presentation state or adapters when QML cannot express a
  truthful label cleanly.
- GUI-focused tests and verification documentation.

### Explicitly excluded

- Signaling, PeerConnection ownership, ICE, capture backends, VideoToolbox,
  Media Foundation, codec selection/fallback, frame queues, bitrate policy,
  resolution policy, capture recovery policy, primary voice transport,
  movie audio, system audio, TURN, remote input, and Movie Stage 2B.
- Friend/accounts/avatar/chat/reactions/recording/file transfer/annotation,
  screenshots, HDR, theme marketplace, statistics charts, or other feature
  expansion.
- Fake device pickers, fake OS-settings buttons, or controls for unsupported
  capabilities.

## Design Direction

### Shared visual system

`ShareMeTheme.qml` is the source of truth for the visual system. It exposes
semantic tokens for:

- deep application background;
- elevated surface and raised surface;
- border and divider;
- primary, secondary, and tertiary text;
- one restrained blue accent and its hover/pressed variants;
- success, warning, error, focus, disabled, and scrim states.

The dark direction remains, but large surfaces and text avoid pure black and
pure white. The palette should feel comfortable during a long call and must
not use glowing borders or category-specific accent colors.

Geometry follows a small shared scale:

- spacing: `4 / 8 / 12 / 16 / 24 / 32 / 48`;
- radii: `6 / 10 / 14` for controls, fields, surfaces, and sheets;
- minimum interactive height: 44 px;
- regular details drawer: approximately 320 px;
- normal transition duration: 140–180 ms.

Typography uses the platform system UI font with explicit roles for display,
page title, section title, body, label, metadata, and caption. Room codes and
technical values may use a stable numeric/monospace treatment. Product copy
does not use implementation enum names as its main label.

The current text-symbol controls are replaced by a local `IconGlyph`-style
primitive with one consistent line weight and deterministic rendering. It must
not require remote assets or emojis. Each icon control retains an accessible
name, tooltip, visible focus ring, hover state, pressed state, disabled state,
and a 44 px hit target. State is never conveyed by color alone.

No continuous decorative animation, shader, full-stage blur, frame-related QML
work, or independent visual polling is allowed.

## Page Design

### Home

Home is a quiet launch surface with the following hierarchy:

1. ShareMe mark and concise Chinese product sentence.
2. Dominant `创建房间` action.
3. Secondary `加入房间` action.
4. At most one recent-room row with readable formatted code, `重新加入`, and
   `忘记`.
5. Quiet Settings and Help actions in the top bar.

The decorative gradient preview panel is removed. Home does not show codec,
bitrate, capture, route, or developer information. Recent-room controls must
retain readable contrast in normal, hover, pressed, and disabled states.

### Preflight

Create and join use one centered preparation surface, not the current large
preview/settings split. The order is:

1. `房间`;
2. `设备`;
3. `共享质量`;
4. primary action.

Create mode explains that the current screen will be shared and lets the user
choose a quality profile. Join mode puts the room code first, normalizes it via
the existing C++ validation, displays it as `ABC-234`, and keeps the action
disabled until validation succeeds.

The existing real microphone and speaker enable/disable intents remain. The
current presentation/controller contract does not expose a safe device
selection operation, so this stage does not add microphone or speaker dropdowns
or invent device names. Device selection is a future GUI opportunity, not a
refinement workaround.

Quality profiles retain their protocol values but use user-facing copy:

- `1080p 60 · 流畅` — suitable for most networks;
- `1440p 60 · 高画质` — recommended when the network allows it;
- `4K 30 · 影院` — suited to film or mostly static content.

No label exposes `standard`, `quality`, or `cinema` as the primary user-facing
description. Compact widths use one column and keep the action above the
visible fold.

### Active call

The call page is a focus stage:

- one dominant `VideoOutput` with preserve-aspect-fit and restrained neutral
  letterboxing;
- a low-profile 52–56 px top bar;
- a compact floating control group at the bottom center of the stage;
- an on-demand drawer that overlays the stage rather than permanently reducing
  its width.

The top bar contains ShareMe, formatted room code with copy action, and one
human-readable connection state. Codec, bitrate, FPS, generation, encoder
implementation, and packet counters are not shown there.

Waiting states are minimal, for example `正在等待屏幕共享…`, with a subtle
status mark. Temporary capture recovery remains in the call and shows
`正在恢复屏幕共享` without replacing the stage with an alarming dialog.
Existing safe-frame behavior is preserved; QML does not alter frame ownership.

The normal dock contains, in order:

- microphone;
- speaker;
- details;
- leave.

The current disabled share button is removed from the normal dock because it
does not perform an operation. The component remains structured so a future
truthful control can be added without rearranging the whole dock.

Muted microphone/speaker states use icon shape plus accessible text. Leave is
visually distinct but restrained and does not turn the whole dock red.

At compact widths the video stage shrinks before essential controls become
inaccessible. The details drawer becomes a bottom sheet.

### Details drawer

The drawer is closed by default and uses semantic groups:

- `连接`: state, room, and role;
- `画面`: host/viewer role, screen-sharing state, and selected profile;
- `声音`: microphone, speaker, voice quality, and existing volume control;
- `播放`: only when the existing movie controller exposes real playback
  controls;
- `高级信息`: collapsed technical diagnostics.

Normal information uses human-readable product copy. Resolution, FPS, device
names, and network health are not fabricated when the current controller does
not expose them. Existing technical values remain available under `高级信息`,
including codec, encoder, hardware status, capture profile, presentation
counters, audio route/queue values, underruns, drift, and scheduler values.

Rows align labels and values, long values elide or wrap safely, and the drawer
has a visible close action, logical keyboard focus, and compact bottom-sheet
behavior. It consumes existing controller snapshots and adds no QML timer or
per-frame update.

### Settings and Help

Settings remains a compact dialog. It exposes only the current connection
service address because that setting has real behavior. It is presented as a
development/advanced connection setting, not a primary product task. Unsupported
device switching and experimental media switches remain absent.

Help answers immediate product questions in concise Chinese:

- how to create a room;
- how to join a room;
- why shared video may be missing;
- why voice may be missing;
- why macOS screen-recording permission is required.

Keyboard guidance stays short and does not duplicate the README or describe
internal architecture.

### Recovery and errors

Temporary recovery remains an in-call status. Terminal failures use a calm
result surface with a specific title/message, a primary `重试` action, and a
secondary `返回首页` action. Permission copy explains the required system
action. No direct system-settings button is shown unless a truthful platform
navigation operation exists.

Raw error categories, OS error domains, HRESULTs, C++ exception text, SDP, ICE,
paths, and credentials never appear in normal product copy. Sanitized internal
categories remain available for tests and logs and may be shown only in an
appropriate advanced diagnostic context.

## QML and Presentation Architecture

The dependency direction is:

`ShareMeTheme -> reusable primitives -> product components -> pages -> Main`

Existing components are refined before new ones are added. Likely reusable
changes are a local icon primitive, consistent surface/dialog styling, grouped
section headings, and shared status/copy behavior. `Main.qml` remains a small
application-window host. Pages do not construct or destroy media objects.

Presentation mappings may be implemented in QML or in a narrow GUI-only
controller adapter when the mapping needs to be shared or tested. They must not
change signaling/media state transitions. `ShareMeAppController` and
`RtcDemoController` remain the ownership boundaries.

Existing GUI smoke `objectName` selectors are preserved where possible:
`callPage`, `microphoneControl`, `speakerControl`, `detailsControl`,
`leaveControl`, `voicePanel`, and `speakerVolumeControl`. Legitimate hierarchy
changes update the contract rather than weakening it.

## Interaction and Accessibility

- Primary actions are reachable within the first focus cycle.
- Tab order follows visual order and remains valid in compact layouts.
- Enter/Space activates buttons; Escape closes dialogs/drawers where supported.
- Focus is visible against every surface.
- Tooltips exist for icon-only controls whose meaning is not obvious.
- Muted and disabled states include accessible text and are not color-only.
- Text wraps or elides safely at compact and high-DPI sizes.
- Layouts use Qt Quick Layouts and anchors rather than fixed coordinates.

## Testing and Acceptance

### Automated

- Preserve controller tests for home, preflight, call, error, retry, leave,
  return-home, recent room, and real microphone/speaker operations.
- Extend QML contracts for home, create, join, host call, viewer call, details
  closed/open, settings, help, recovery, compact layout, focusable primary
  actions, and sanitized copy.
- Preserve explicit CLI validation/autostart contracts and all screen/voice and
  movie regression suites.
- Run focused tests, affected full CTest suites, GUI smoke contracts, and
  `git diff --check` from the final tree.

### Manual visual review

Inspect at minimum:

- Home;
- create preflight;
- join preflight;
- active host call;
- active viewer call;
- details open;
- settings;
- help;
- temporary recovery;
- terminal error.

Review compact, normal laptop, and large desktop sizes, keyboard focus, hover,
pressed, disabled, and high-DPI-safe layout behavior. Use screenshots as
temporary evidence only; do not commit them.

### Performance and platform evidence

Compare idle CPU/RSS against the current complete-GUI evidence of 4.95% mean
CPU and 83,440 KiB maximum RSS from the macOS offscreen probe. A modest change
is acceptable; a major unexplained regression blocks completion. Do not claim
physical temperature, audible voice quality, Windows native behavior, or
human visual acceptance without the matching environment evidence.

Fresh configure/build and relevant CTest evidence on macOS is verified only for
macOS. Windows remains environment-dependent unless executed on Windows.

## Completion Gate

The refinement is complete only when:

- all pages share the tokenized visual system;
- Create/Join are immediately understandable;
- the shared screen dominates the call page;
- microphone, speaker, details, and leave are immediately accessible;
- advanced diagnostics are hidden from the normal workflow;
- temporary recovery and terminal errors are calm and understandable;
- QML/controller tests and existing media regressions pass;
- no RTC/media behavior changes are introduced;
- fresh build, GUI tests, relevant full CTest, manual state review, performance
  comparison, and `git diff --check` are completed;
- the final report separates verified macOS evidence from Windows/environment
  boundaries and records the highest-value remaining GUI limitation.

## Parked UX Opportunities

- Safe microphone and speaker device selection after a reviewed controller
  operation exists.
- Truthful direct navigation to platform permission settings where supported.
- A richer connection-health presentation only after the controller exposes a
  stable user-facing health contract.
