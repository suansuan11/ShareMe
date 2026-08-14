# GUI Refinement Verification

## Outcome

`verified-macos-gui-refinement` with explicit partial and
environment-dependent boundaries for attended compact resizing, Windows native
rendering, human A/V acceptance, and physical temperature.

This document verifies the GUI/UX refinement stage. It does not replace the
historical native screen and voice evidence in [Complete GUI Verification](complete-gui.md).

## Identity And Scope

- Branch: `codex/gui-refinement`
- Starting source SHA: `c373679163a22e5ef8bbc6ad1625846a6a20d1d5`
- Final implementation/source SHA: `c373679163a22e5ef8bbc6ad1625846a6a20d1d5`
- Evidence commit: the focused `docs: record GUI refinement verification`
  commit created after this source tip; its exact branch SHA is recorded in the
  Task 6 report because this document is part of that commit.
- Worktree: `/Users/dio/project/ShareMe/.worktrees/gui-refinement`
- Platform: macOS 26.6.1 (25G76), arm64
- Python: 3.14.6
- CMake/CTest: 4.3.3
- Go: 1.26.5 darwin/arm64

Task 6 changed documentation only. The audited refinement branch changes the
following GUI surface groups:

- Visual system: `ShareMeTheme`, `IconGlyph`, `DialogSurface`, `IconControl`,
  `PrimaryButton`, `InfoRow`, and `StatusBanner`.
- Home and preflight: responsive Home, Create, and Join pages; room-code
  validation; user-facing quality labels; current microphone/speaker intent
  switches; no fake device picker.
- Active call: `CallPage`, `CallTopBar`, `VideoStage`, and
  `CallControlDock`, including the four essential controls and one
  `VideoOutput`.
- Details and secondary surfaces: semantic connection/video/audio sections,
  collapsed advanced information, Settings, Help, Recovery, and terminal
  result presentation.
- Contracts and seams: nine GUI states, exact `GUI_STATE`/`GUI_OBJECT`/
  `GUI_ACTION` markers, strict UTF-8 handling, sanitized normal diagnostics,
  and atomic temporary artifacts.

## Design And UX Result

- Shared semantic tokens now own colors, spacing, radii, typography, control
  heights, drawer geometry, focus, and motion values.
- Home gives `创建房间` primary weight, `加入房间` secondary weight, and keeps
  one readable recent-room row without capability or developer claims.
- Create and Join use one preparation hierarchy: `房间`, `设备`, and `共享质量`.
  Quality labels are `1080p 60 · 流畅`, `1440p 60 · 高画质`, and `4K 30 · 影院`.
- The call stage keeps the shared screen dominant, uses calm Chinese status
  copy, and exposes microphone, speaker, details, and leave without a fake
  share operation.
- Normal details are grouped and advanced implementation values stay behind a
  closed-by-default `高级信息` disclosure.
- Settings and Help use the same modal surface and visible focus treatment.
  Recovery maps sanitized categories to calm titles and keeps `重试` and
  `返回首页` as the real actions.

## Automated Evidence

All counts below are from fresh commands in this worktree on the stated macOS
host.

| Area | Result |
| --- | --- |
| Fresh `call-dev` configure/build | Verified; CMake found preserved WebRTC root; configure emitted only the existing optional Vulkan-header notice; build exited 0. |
| Fresh `movie-call-dev` configure/build | Verified; FFmpeg and WebRTC found; Ninja completed `417/417`; only existing Qt deprecation, variadic-macro, and duplicate-library warnings appeared. |
| `call-dev` CTest | `57/57` passed in `22.73 s`. |
| `movie-call-dev` CTest | `82/82` passed in `41.41 s`. |
| GUI QML contract | `22` tests passed in `8.243 s` using the supported `--demo` entry point. |
| GUI smoke Python contract | `16` tests passed in `1.491 s`. |
| Windows GUI contract logic | `10` tests passed, `1` fixture-dependent skip, in `0.543 s`; this is not native Windows evidence. |
| Real GUI smoke | `GUI_SMOKE status=verified probes=9 idle_samples=12`. |
| Controller/CLI regression filter | `7/7` CTest tests passed in `3.53 s`. |
| Go race tests | All signaling packages passed; the command package reported no test files. |
| Go vet | Exit 0 with no output. |
| Sol-Terra workflow | `8` tests passed in `0.127 s`. |
| ShareMe skill validator | `Skill is valid!` |
| `git diff --check` | Exit 0 with no output. |

The brief's literal command `python3 -m unittest tests/scripts/gui_qml_contract_test.py`
was also run. It produced `Ran 22 tests` with `21` `PermissionError` errors
because this module intentionally requires its `--demo` parser and otherwise
uses `Path()` (`.`) as the executable. The supported equivalent,
`python3 tests/scripts/gui_qml_contract_test.py --demo build/call-dev/client/tools/rtc_demo/shareme_rtc_demo`,
passed all `22/22`; no source change was made for the invocation mismatch.

The fresh smoke artifact is temporary and ignored:

- `/var/folders/zf/yfwlct4x0yxdnf334_1cd13h0000gn/T/opencode/gui-refinement-smoke-final.json`
- SHA-256: `b90c2a69fb4f64375d1fb1add6e84059f6f565881d85acddfa0fb3d8e2958b60`

Its nine states were `home`, `create`, `join`, `settings`, `help`, `recovery`,
`call-host`, `call-viewer`, and `call-host-actions`. The action probe reported
successful microphone and speaker toggles, details open, rejected volume
change restored, leave, return Home, and advanced disclosure expansion.

## Manual macOS Review

Cocoa window captures and logs were stored only under:

`/var/folders/zf/yfwlct4x0yxdnf334_1cd13h0000gn/T/opencode/`

Representative captures from the fresh `call-dev` executable include:

- Home: `gui-refinement-home-window.png`
- Create: `gui-refinement-create-normal.png`
- Join: `gui-refinement-join-normal.png`
- Active host: `gui-refinement-call-host-live.png`
- Active viewer: `gui-refinement-call-viewer-normal.png`
- Details open after the controlled native recovery seam: `gui-refinement-temporary-recovery-main.png`
- Settings: `gui-refinement-settings-normal.png`
- Help: `gui-refinement-help-normal.png`
- Terminal result/recovery preview: `gui-refinement-recovery-normal.png`
- Cocoa scale-factor compact sample: `gui-refinement-create-compact-scale.png`
- Cocoa scale-factor large sample: `gui-refinement-home-large-scale.png`

At the default Cocoa size, the main window was `1100x732` including the native
title bar, corresponding to the normal laptop review. The captures showed no
visible clipping or hidden primary action at that size. The active host/viewer
captures showed the waiting states `正在准备屏幕共享…` and `正在等待屏幕共享…`,
the low-profile top bar, and the four-control dock. The details capture showed
the drawer groups and the collapsed `高级信息` control. Settings and Help
opened with a visible focus outline on the close control. The recovery preview
showed the calm generic terminal result with `重试` and `返回首页`.

The controlled macOS native delegate-fault seam also produced these exact
sanitized log markers in `gui-refinement-screen-host-3.log`:

```text
SMOKE_STATUS native-delegate-fault-injected
SMOKE_STATUS native-old-stream-stopped
SMOKE_STATUS screen-capture-restarted
```

The resulting capture is post-recovery evidence with the details surface
visible; it is not claimed as a still frame of the short-lived
`正在恢复屏幕共享` text.

The true compact minimum was exercised by the GUI contract with
`QT_QPA_PLATFORM=offscreen:size=760x520`, including clean Create loading,
required controls, and `Main.qml` minimum dimensions of `760x520`. Native
attended resizing to exactly `760x520` was not available: macOS System Events
returned Accessibility error `-1719` in this session. The `QT_SCALE_FACTOR=0.75`
capture measured `825x557` physical Cocoa bounds and is labeled a scale-factor
sample, not logical compact-layout proof. The `QT_SCALE_FACTOR=1.25` capture
measured `1375x845` physical bounds and is likewise a large-scale visual sample,
not a claim about a physical large desktop or DPI matrix.

Keyboard, hover, pressed, disabled, mute, speaker, details, leave, copy, retry,
and return-home contracts were exercised by the nine-state smoke and QML/GUI
contract suites. Human keyboard/mouse acceptance is partial because native
Accessibility input injection was unavailable; no human acoustic or visual
two-person acceptance is claimed.

## Performance

The comparison uses the existing complete-GUI macOS offscreen baseline, not a
physical temperature measurement.

| Metric | Complete-GUI baseline | Fresh GUI refinement | Change |
| --- | ---: | ---: | ---: |
| Idle mean CPU | `4.95%` | `0.525%` | `-4.425` percentage points (`-89.394%`) |
| Idle maximum RSS | `83,440 KiB` | `80,672 KiB` | `-2,768 KiB` (`-3.317%`) |
| Fresh idle samples | `12` | `12` | same sample count |

The fresh result is a modest non-regression in this bounded offscreen probe.
Neither result is a physical temperature, sustained energy, or thermal claim.
The final artifact recorded `cpuMaxPercent=6.299`, `cpuMeanPercent=0.525`,
`rssMaxKiB=80672`, and `sampleCount=12`.

## Architecture And Platform Boundaries

- **Verified — macOS:** fresh call/movie configurations and CTest suites,
  GUI contracts, nine-state smoke, sanitized markers, compact offscreen load,
  controller/CLI regressions, Go race/vet, workflow, skill validation, and
  bounded Cocoa visual captures.
- **Verified — no RTC/media behavior change:** `git diff main...HEAD` has no
  changes under `client/core`, `client/rtc`, `client/signaling`,
  `client/media`, `rtc_demo_controller.cpp`, or `shareme_app_controller.cpp`.
  The only C++ implementation change is the existing GUI smoke seam in
  `main.cpp`; it does not construct a session for the surface-only probes.
  No signaling, PeerConnection, capture, codec, queue, bitrate, resolution,
  recovery policy, voice, movie audio, or media/controller ownership changed.
- **Verified — cache preservation:** the external WebRTC checkout remained
  detached and clean, depot tools remained on `main` and clean, and the
  manifest SHA-256 remained
  `28280280e8b50496a618ae6ca961d5fdd65abd4b361b53c96ee7f4a9a7e27143`.
- **Environment-dependent — Windows:** no Windows native executable,
  Cocoa-equivalent rendering, Windows DPI, keyboard traversal, native device
  behavior, two-device visual/acoustic acceptance, or Windows performance run
  was available. Windows contract logic passing on macOS does not verify
  Windows.
- **Environment-dependent — human/physical acceptance:** no two-person human
  visual or acoustic gate, physical display scanout, physical temperature, or
  thermal observation was run.

The historical native media evidence and its claims remain in
`complete-gui.md`; this refinement did not repeat or weaken those gates.

## Known Limitations And Parked UX

- Attended native logical `760x520` and large-desktop resize review remain
  partial because Accessibility control was unavailable in this macOS session.
- The transient recovery label was exercised through the controlled native
  diagnostic seam and its sanitized markers; the retained screenshot is
  post-recovery rather than a proof of the transient frame.
- Safe microphone and speaker device selection remains parked until the
  controller exposes a reviewed truthful operation. No fake device selector was
  added.
- Direct platform permission navigation remains parked until a truthful native
  operation exists.
- A richer connection-health presentation remains parked until a stable
  user-facing controller contract exists.
