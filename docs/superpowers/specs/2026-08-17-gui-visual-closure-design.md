# GUI Visual Closure Design

## Outcome

Close the unfinished `codex/gui-refinement` stage by turning its oversized,
debug-like surfaces into a compact desktop product. Preserve every existing
application and call action while reducing visual noise, eliminating redundant
hover prompts, and making normal call information distinct from diagnostics.

## Selected direction

Use a restrained dark desktop-tool language across macOS and Windows:

- one deep background, two surface levels, subtle one-pixel dividers, and one
  blue accent;
- 40-44 px hit areas with 17-18 px thin line icons;
- text buttons explain themselves and never repeat the same wording in a
  tooltip;
- icon-only controls retain accessible names but do not open automatic hover
  bubbles during normal use;
- focus remains visible for keyboard users through borders, not tooltips;
- native Basic-style progress, slider, disclosure, combo, and switch surfaces
  are replaced or wrapped by the shared dark tokens where they are visible.

## Page closure

Home uses a centered 680 px workspace with a single-line product statement,
two compact actions, and a quiet recent-room row. Settings and Help become
36-40 px tertiary icon actions instead of large bordered squares.

Preflight becomes a compact preparation panel: short title and one sentence,
screen/room summary, two concise audio intent rows, quality selection, and the
primary action. It removes duplicate headings and parenthetical “current
intent” copy. The primary action remains visible at the default 1100x732 and
logical 760x520 minimum.

The active call keeps the stage dominant. The control dock becomes a 60 px
floating capsule with four 40 px actions. Details remains optional, but its
normal view contains only a compact session summary and a single voice section.
All implementation counters and duplicated source/profile values remain behind
the collapsed advanced disclosure. Progress, volume, and disclosure controls
use themed tracks and buttons rather than white Basic-style widgets.

## Interaction and accessibility

Accessible names remain unchanged. Keyboard focus, click targets, mute state,
speaker state, details, leave, copy, retry, and return-home actions remain
functional. A tooltip is allowed only when a control has no visible label and
the explanation adds information not already obvious; this closure uses no
automatic tooltips in the primary navigation or call dock.

## Verification

Tests must reject reintroduced automatic tooltips in shared primary controls,
require the compact dock and themed voice/disclosure surfaces, and keep all
existing functional GUI markers. Fresh macOS rendering must be captured for
Home, Create, Join, and active call/details at default size, plus Create at the
logical compact size. Visual acceptance checks clipping, hierarchy, native
white-control leaks, tooltip bubbles, icon consistency, and primary-action
visibility. Windows rendering remains environment-dependent until rerun there.

## Frozen boundaries

No controller/media/signaling behavior, quality profile value, accessibility
action, room-code contract, audio path, screen path, recovery policy, build
dependency, or external WebRTC cache changes in this closure.
