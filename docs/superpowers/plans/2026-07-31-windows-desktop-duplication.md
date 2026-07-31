# Windows Desktop Duplication Capture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a Windows host video source that captures the primary display through Desktop Duplication and sends real frames through the existing one-to-one WebRTC demo.

**Architecture:** Add a Windows-only `ShareMe::DesktopRTC` target with a deterministic BGRA-to-I420 converter and a `LocalVideoSource` implementation owning D3D11/DXGI lifetime. Inject it into the existing RTC demo only for an explicit Windows host `--source desktop` selection, leaving portable and macOS targets unchanged.

**Tech Stack:** C++20, D3D11, DXGI 1.2 Desktop Duplication, libyuv, native libwebrtc, Qt 6, CMake/CTest.

## Global Constraints

- Preserve locked WebRTC revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97` and its existing dependency build flow.
- Use `IDXGIOutputDuplication`; do not use GDI, `BitBlt`, or periodic screenshots.
- Target primary-display 1080p at up to 60 fps, with one GPU readback and no intermediate full-frame CPU copy in the normal path.
- Compile all D3D11/DXGI code only under `WIN32`; preserve macOS build and runtime paths.
- Keep existing architecture and avoid unrelated refactoring.

---

### Task 1: Deterministic mapped-texture conversion

**Files:**
- Create: `client/rtc/desktop/src/desktop_frame_converter.hpp`
- Create: `client/rtc/desktop/src/desktop_frame_converter.cpp`
- Create: `tests/rtc/desktop_frame_converter_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `MappedBgraFrame { const std::uint8_t *data; int width; int height; std::size_t row_pitch; }`.
- Produces: `webrtc::scoped_refptr<webrtc::I420Buffer> convert_mapped_bgra_to_i420(const MappedBgraFrame &) noexcept`.
- Consumed by: `DesktopCaptureSource::deliver_texture` in Task 2.

- [ ] **Step 1: Write the failing converter test**

Create a 4x2 BGRA buffer with padded row pitch. Assert conversion returns a
4x2 I420 buffer, produces non-uniform luma for black/white pixels, and ignores
padding. Assert null data, non-positive dimensions, row pitch below
`width * 4`, and checked byte-size overflow return null.

- [ ] **Step 2: Run the focused test and verify RED**

Run the movie-audit configure/build for target
`shareme_desktop_frame_converter_test`; expect failure because the converter
header and target do not exist.

- [ ] **Step 3: Implement minimal conversion**

Define the internal view and validate dimensions with checked `size_t`
multiplication. Allocate `webrtc::I420Buffer::Create(width, height)` and call:

```cpp
libyuv::ARGBToI420(
    frame.data, static_cast<int>(frame.row_pitch),
    output->MutableDataY(), output->StrideY(),
    output->MutableDataU(), output->StrideU(),
    output->MutableDataV(), output->StrideV(),
    frame.width, frame.height);
```

Return null on validation or conversion failure.

- [ ] **Step 4: Build and verify GREEN**

Run the focused converter test and confirm all valid, padded-stride, and
invalid-input cases pass.

- [ ] **Step 5: Commit**

```bash
git add client/rtc/desktop/src tests/rtc
git commit -m "feat: convert duplicated desktop frames"
```

### Task 2: Desktop Duplication video source

**Files:**
- Create: `client/rtc/desktop/include/shareme/rtc/desktop_capture_source.hpp`
- Create: `client/rtc/desktop/src/desktop_capture_source.cpp`
- Create: `client/rtc/desktop/CMakeLists.txt`
- Create: `tests/rtc/desktop_capture_source_test.cpp`
- Modify: `client/rtc/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Consumes: `convert_mapped_bgra_to_i420` from Task 1.
- Produces: `DesktopCaptureConfig { int max_frames_per_second{60}; }`.
- Produces: `DesktopCaptureSource::create(DesktopCaptureConfig = {})`, returning `webrtc::scoped_refptr<DesktopCaptureSource>`.
- Implements: every `LocalVideoSource` virtual plus `last_width()` and `last_height()` inspection for hardware acceptance.

- [ ] **Step 1: Write the source contract and hardware smoke test**

Assert invalid fps values are rejected by a pure
`valid_desktop_capture_config`. On Windows with an interactive display, create
the source, attach `CountingVideoSink`, start it, wait up to three seconds for
a frame, and assert positive dimensions, increasing timestamps, screencast
state, no denoising, no error, and shutdown below 500 ms. Return 77 only when
initialization reports no interactive attached output.

- [ ] **Step 2: Run the focused test and verify RED**

Build `shareme_desktop_capture_source_test`; expect failure because the public
source API and `ShareMe::DesktopRTC` target do not exist.

- [ ] **Step 3: Add the Windows-only target**

In `client/rtc/CMakeLists.txt`, add `desktop` only under `WIN32`. Link the new
static target with `ShareMe::WebRTC`, `d3d11`, and `dxgi`; keep D3D headers out
of public headers with a private implementation object.

- [ ] **Step 4: Implement primary-output initialization**

Enumerate adapters and attached `IDXGIOutput1` objects. Prefer the output whose
desktop rectangle contains `(0, 0)`, create a D3D11 hardware device on its
adapter with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`, and call `DuplicateOutput`.
Create the staging texture lazily from the first acquired texture description.

- [ ] **Step 5: Implement acquisition and resource safety**

Run a `std::jthread` loop around `AcquireNextFrame(50, ...)`. Use scope guards
so every successful acquire calls `ReleaseFrame` and every map calls `Unmap`.
Treat timeout as normal, rebuild duplication after `DXGI_ERROR_ACCESS_LOST`,
and store only stable error categories. Requesting stop must be observed within
the 50-ms acquisition bound.

- [ ] **Step 6: Convert, adapt, and emit**

Copy the acquired texture into the persistent staging texture, map it, convert
with Task 1, call `AdaptFrame`, crop/scale only when requested, and build a
`webrtc::VideoFrame` with monotonic microsecond and 90-kHz RTP timestamps.
Map DXGI rotation to WebRTC rotation, cap deliveries at 60 fps, call `OnFrame`,
and update dimensions/counters atomically.

- [ ] **Step 7: Build and verify GREEN**

Run converter and real desktop source tests. Require an actual frame on this
Windows development machine rather than accepting skip.

- [ ] **Step 8: Commit**

```bash
git add client/rtc/desktop client/rtc/CMakeLists.txt tests/rtc
git commit -m "feat: capture Windows desktop with DXGI duplication"
```

### Task 3: Inject desktop capture into the RTC demo

**Files:**
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produces CLI: `--source test|desktop`, default `test`.
- Produces controller mode: `RtcDemoVideoSource::test` or `RtcDemoVideoSource::desktop`.
- Consumes: `DesktopCaptureSource::create()` only when `SHAREME_HAS_DESKTOP_CAPTURE` is defined.

- [ ] **Step 1: Add failing CLI contracts**

Assert `--source invalid` exits 2, viewer `--source desktop` exits 2, and a
non-Windows build without desktop support reports a stable unsupported error.
Keep existing help and required-argument contracts.

- [ ] **Step 2: Run CLI tests and verify RED**

Run `rtc_demo_cli_contract`; expect failure because `--source` is not parsed.

- [ ] **Step 3: Link desktop support conditionally**

When `ShareMe::DesktopRTC` exists, link it to `shareme_rtc_demo` and define
`SHAREME_HAS_DESKTOP_CAPTURE=1`. Do not add Windows libraries directly to the
portable demo target.

- [ ] **Step 4: Implement source selection**

Parse `--source` before constructing the controller. For a Windows host in
desktop mode, set `SignaledVideoMode::injected` and provide a factory returning
`DesktopCaptureSource::create()`. Preserve synthetic video for viewers and the
default mode.

- [ ] **Step 5: Verify CLI and local GUI startup**

Run the CLI contract, start a desktop host against a deliberately unreachable
signaling endpoint to verify source-independent GUI startup, then run host and
viewer through the local server and visually/quantitatively confirm remote
frames.

- [ ] **Step 6: Commit**

```bash
git add client/tools/rtc_demo tests/scripts/rtc_demo_cli_test.py
git commit -m "feat: share Windows desktop from RTC demo"
```

### Task 4: Full regression and delivery evidence

**Files:**
- Create: `docs/verification/windows-desktop-duplication.md`
- Modify: `docs/superpowers/plans/2026-07-31-windows-desktop-duplication.md`

**Interfaces:**
- Consumes: the completed capture source and demo.
- Produces: repeatable Windows build, test, sender, and viewer commands plus measured results.

- [ ] **Step 1: Run complete verification**

Run the call-only 14-test matrix, movie 36-test matrix, Go tests, converter
test, required real-DXGI capture test, and serial synthetic signaled-call smoke.
Run `git diff --check` and confirm the worktree contains no generated files.

- [ ] **Step 2: Run two-process desktop acceptance**

Start the local Go signaling server, launch the host with
`--source desktop`, join from an independent viewer, and require connected
peers plus nonzero remote frames at the primary display dimensions. Record
actual frame count, dimensions, selected ICE candidate type, and shutdown
behavior without recording SDP, candidates, tokens, or paths.

- [ ] **Step 3: Document evidence and limitations**

Record exact commands, results, environment, one-readback architecture,
primary-display-only behavior, missing cursor composition, and deferred native
GPU encoding in `docs/verification/windows-desktop-duplication.md`.

- [ ] **Step 4: Request independent code review**

Review the feature diff against this plan. Resolve every Critical or Important
finding and rerun affected tests.

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/plans/2026-07-31-windows-desktop-duplication.md docs/verification/windows-desktop-duplication.md
git commit -m "docs: verify Windows desktop duplication"
```

- [ ] **Step 6: Merge and push**

After final verification, merge the feature branch into `main`, push without
force, verify `HEAD == origin/main`, and remove the merged worktree and local
feature branch.
