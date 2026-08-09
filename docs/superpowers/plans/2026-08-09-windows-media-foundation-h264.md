# Windows Media Foundation H.264 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an actual Windows Media Foundation H.264 hardware encode/decode path to ShareMe screen sharing without rebuilding locked libwebrtc.

**Architecture:** Keep codec selection platform-neutral and add a Windows-only Media Foundation adapter behind the existing WebRTC factories. Separate pure NV12/Annex-B transformations from COM/MFT lifecycle code so deterministic tests run without hardware, while a native two-peer smoke remains the authority for hardware activation.

**Tech Stack:** C++20, locked libwebrtc `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`, Windows Media Foundation, WRL `ComPtr`, libyuv, CMake, MSVC, Python smoke contracts.

## Global Constraints

- Do not modify, download, or rebuild the repository-external libwebrtc cache.
- Keep `client/core` free of Windows, Qt, FFmpeg, and libwebrtc dependencies.
- Preserve macOS VideoToolbox behavior and non-Windows build isolation.
- Keep every input/output queue bounded and observable.
- Report hardware active only after a hardware MFT is activated and initialized.
- Fall back to the existing VP8 standard profile on every selection-time failure.
- Do not claim zero-copy or CPU improvement in this stage.

---

### Task 1: Platform-neutral H.264 selection

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/video_encoder_selection.hpp`
- Modify: `client/rtc/webrtc/src/video_encoder_selection.cpp`
- Modify: `client/rtc/webrtc/src/macos_video_encoder_selection.mm`
- Modify: `tests/rtc/video_encoder_selection_test.cpp`

**Interfaces:**
- Produces: `PlatformH264Probe`, `PlatformH264Factory`, `probe_platform_h264_encoder`, and `create_platform_h264_encoder_factory`.
- Preserves: `select_screen_video_encoder(profile, probe, factory)` and VP8 fallback semantics.

- [ ] **Step 1: Write a failing selection test**

Add a fake factory implementation label and assert that an injected Windows-style
selection returns `encoder_implementation == "MediaFoundation"` rather than the
hard-coded `"VideoToolbox"`.

- [ ] **Step 2: Run RED**

Run:

```powershell
cmake --build build/call-dev --target shareme_video_encoder_selection_test -j 2
build/call-dev/shareme_video_encoder_selection_test.exe
```

Expected: failure because selection always reports `VideoToolbox`.

- [ ] **Step 3: Implement the minimal generic contract**

Pass the platform implementation label with the injected factory, rename the
platform entry points, and make the non-Apple/non-Windows stub return
`platform-unavailable`. Keep the existing level adaptation and initialization
probe unchanged.

- [ ] **Step 4: Run GREEN and regression**

Run the focused executable and `ctest --test-dir build/call-dev -R
video_encoder_selection --output-on-failure`.

- [ ] **Step 5: Commit**

```powershell
git add client/rtc/webrtc tests/rtc/video_encoder_selection_test.cpp
git commit -m "refactor: generalize platform H264 selection"
```

### Task 2: Deterministic Windows H.264 buffer contracts

**Files:**
- Create: `client/rtc/webrtc/src/windows_mf_h264_buffers.hpp`
- Create: `client/rtc/webrtc/src/windows_mf_h264_buffers.cpp`
- Create: `tests/rtc/windows_mf_h264_buffers_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `copy_i420_to_nv12(const webrtc::I420BufferInterface&, std::span<std::byte>, int)`.
- Produces: `normalize_h264_access_unit(std::span<const uint8_t>, std::vector<uint8_t>&)` returning keyframe and validity metadata.

- [ ] **Step 1: Write failing pure tests**

Cover even-sized I420 plane layout, padded NV12 pitch, undersized output, Annex-B
passthrough, 4-byte AVCC conversion, malformed NAL lengths, SPS/PPS retention,
and IDR keyframe detection.

- [ ] **Step 2: Run RED**

Build the new test target. Expected: configure/build fails because the buffer
interfaces do not exist.

- [ ] **Step 3: Implement minimal conversions**

Use `libyuv::I420ToNV12`; parse the entire access unit before mutating the output
so malformed input never produces partial data. Bound all size arithmetic with
checked `size_t` comparisons.

- [ ] **Step 4: Run GREEN**

Run the focused buffer test and the existing desktop converter test.

- [ ] **Step 5: Commit**

```powershell
git add client/rtc/webrtc/src/windows_mf_h264_buffers.* tests/rtc
git commit -m "feat: add Windows H264 buffer contracts"
```

### Task 3: Media Foundation hardware encoder

**Files:**
- Create: `client/rtc/webrtc/src/windows_mf_h264_encoder.hpp`
- Create: `client/rtc/webrtc/src/windows_mf_h264_encoder.cpp`
- Create: `client/rtc/webrtc/src/windows_mf_video_codec.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Modify: `tests/rtc/video_encoder_selection_test.cpp`

**Interfaces:**
- Produces: `probe_platform_h264_encoder(width, height, reason)`.
- Produces: `create_platform_h264_encoder_factory()`.
- Consumes: NV12 and Annex-B helpers from Task 2.

- [ ] **Step 1: Write failing platform contract tests**

Assert stable reasons for no hardware activation, invalid dimensions, and failed
initialization. Add a Windows native probe test that returns skip code 77 only
when no hardware H.264 MFT exists; any activation/configuration error fails.

- [ ] **Step 2: Run RED**

Expected: the probe returns `platform-unavailable` and the native probe test
cannot select Media Foundation.

- [ ] **Step 3: Implement hardware enumeration and lifecycle**

Use `MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE |
MFT_ENUM_FLAG_SORTANDFILTER, H264 input/output types)`, activate candidates, set
`MF_TRANSFORM_ASYNC_UNLOCK` for asynchronous MFTs, configure H.264 output and
NV12 input, send begin/start messages, and reject candidates that cannot reach
the configured state. Link `mfplat`, `mfuuid`, `mf`, `mfreadwrite`, and `ole32`
only on WIN32.

- [ ] **Step 4: Implement bounded encode flow**

Convert I420 to a reusable NV12 sample, preserve WebRTC RTP timestamp/capture
time, request clean points for keyframes, drain `ProcessOutput`, normalize the
access unit, and invoke `EncodedImageCallback`. Handle `MF_E_NOTACCEPTING` with
one drain/retry and never add an unbounded frame queue. Apply `SetRates` through
`ICodecAPI` when supported.

- [ ] **Step 5: Run GREEN**

Run buffer, selection, native probe, and WebRTC loopback tests.

- [ ] **Step 6: Commit**

```powershell
git add client/rtc/webrtc tests/rtc
git commit -m "feat: implement Media Foundation H264 encoder"
```

### Task 4: Media Foundation H.264 receiver decoder

**Files:**
- Create: `client/rtc/webrtc/src/windows_mf_h264_decoder.hpp`
- Create: `client/rtc/webrtc/src/windows_mf_h264_decoder.cpp`
- Modify: `client/rtc/webrtc/src/windows_mf_video_codec.cpp`
- Modify: `client/rtc/webrtc/src/webrtc_runtime.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Create: `tests/rtc/windows_mf_h264_decoder_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: Windows implementation of `create_platform_video_decoder_factory()`.
- Consumes: normalized Annex-B access units and WebRTC decoded-image callback.

- [ ] **Step 1: Write failing decoder factory/lifecycle tests**

Assert Windows advertises H.264 packetization-mode 1, creates a decoder, rejects
malformed samples, and permits repeated `Configure`/`Release` without leaked
callback ownership.

- [ ] **Step 2: Run RED**

Expected: the existing Windows platform decoder factory returns null.

- [ ] **Step 3: Implement decoder MFT**

Enumerate H.264 decoder MFTs, prefer hardware-aware transforms but allow the
Windows system decoder because only encoder hardware is required. Configure
H.264 input and NV12 output, handle stream-change output types, convert NV12 to
I420, and propagate WebRTC timestamps and dimensions.

- [ ] **Step 4: Run GREEN and peer regressions**

Run decoder, WebRTC loopback, signaled peer, and screen source tests.

- [ ] **Step 5: Commit**

```powershell
git add client/rtc/webrtc tests/rtc
git commit -m "feat: add Media Foundation H264 decoding"
```

### Task 5: Native acceptance, evidence, and delivery

**Files:**
- Modify: `scripts/run_screen_stream_smoke.py`
- Modify: `tests/scripts/screen_stream_smoke_test.py`
- Modify: `docs/verification/hardware-screen-streaming.md`
- Modify: `docs/development/current-stage.md`

**Interfaces:**
- Produces: Windows smoke mode requiring active Media Foundation H.264 while
  retaining explicit `--allow-software-fallback` diagnostics mode.

- [ ] **Step 1: Write a failing smoke contract test**

Require Windows hardware mode to accept only `H264`, `MediaFoundation`, and
`hardware_encoder_status=active`; reject VP8 and fallback statuses.

- [ ] **Step 2: Run RED**

Run `tests/scripts/screen_stream_smoke_test.py`. Expected: the runner has no
Windows hardware acceptance mode.

- [ ] **Step 3: Implement the smoke gate**

Make hardware mode the Windows default. Keep `--allow-software-fallback` only
for explicit diagnostics and ensure summary output records the implementation.

- [ ] **Step 4: Run full verification**

Run fresh `call-dev` and `movie-call-dev` builds/CTest, Go test/vet, workflow
tests, skill validation, bootstrap contracts, `git diff --check`, and the native
10-second standard hardware screen/voice smoke. Run quality and cinema gates
separately and label their actual results.

- [ ] **Step 5: Review and document exact evidence**

Record GPU/driver scope without identifiers beyond the adapter model, negotiated
codec, implementation, dimensions, frame counters, bitrate, voice continuity,
recovery, and remaining zero-copy/physical-display boundaries.

- [ ] **Step 6: Commit and integrate**

```powershell
git add scripts tests/scripts docs
git commit -m "feat: verify Windows hardware screen encoding"
git fetch origin
git rebase origin/main
```

After merged-main verification, merge with a focused `feat:` commit, push
`main`, and verify `refs/heads/main` equals the local merge SHA.
