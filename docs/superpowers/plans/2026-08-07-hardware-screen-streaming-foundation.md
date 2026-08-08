# Hardware Screen Streaming Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Make macOS hardware screen video the primary ShareMe video source while preserving the existing WebRTC signaling, voice, movie, and audio-route behavior.

**Architecture:** Keep the current WebRTC PeerConnection and signaling layers. Add a narrow screen profile and native frame seam outside portable core, feed ScreenCaptureKit CVPixelBuffer-backed frames into the existing injected local-video source, and select the existing WebRTC Objective-C VideoToolbox H.264 factory through a controlled external target expansion. Keep VP8 software as an explicit bounded fallback and retain the existing Windows DesktopCaptureSource as a compatibility path.

**Tech Stack:** C++20, Objective-C++, Qt/QML, ScreenCaptureKit, CoreVideo, VideoToolbox, libwebrtc revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`, existing WebRTC signaling and voice paths, CTest, Python contract tests.

## Global Constraints

- Screen streaming is the primary video mode; Movie Mode remains advanced/experimental and is not extended.
- Required profiles are 1920x1080@60, 2560x1440@60, and 3840x2160@30; 3840x2160@60 is optional only after required profiles pass.
- H.264 hardware encoding is first choice; do not compare codec families or add a second codec project.
- Use the current WebRTC revision; do not update Qt, FFmpeg, unrelated libraries, or external WebRTC source.
- Preserve the old external `out/shareme` cache and use the separate feasibility/accepted output for any new WebRTC artifacts.
- Keep `client/core` free of Qt, WebRTC, FFmpeg, GPU SDK, and operating-system headers.
- Never make the primary native path GPU texture -> CPU staging/map -> CPU I420 -> encoder.
- Do not add system-audio capture, HDR, Movie Stage 2B, movie correlation, hard resync, remote input, TURN, public-network certification, Linux hardware encoding, or 4K60 optimization.
- Capture callbacks never block on WebRTC or Qt; every capture and presentation queue is bounded to the latest frame.
- Voice, signaling, MovieAudioRenderer, audio-route switching, and Movie Mode regressions must remain green.
- `WEBRTC_ROOT` names the external root passed to `scripts/bootstrap_webrtc.py`; the checkout is `${WEBRTC_ROOT}/checkout`. The legacy `shareme` output uses `${WEBRTC_ROOT}/shareme-webrtc-manifest.json`; versioned outputs use a matching versioned manifest and `WEBRTC_OUTPUT_NAME` selects it in CMake.

## Feasibility Result

Task 1 is complete on macOS ARM64 with Xcode 26.6:

- `main` starts at `d8218a5`.
- The locked checkout is at revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`.
- Existing ShareMe uses `rtc_use_h264=false` and a VP8 template factory in `client/rtc/webrtc/src/webrtc_runtime.cpp`; this is not hardware encoding.
- The locked checkout contains `sdk:videotoolbox_objc`, `sdk:native_api`, `RTCVideoEncoderH264`, `ObjCToNativeVideoEncoderFactory`, `ObjCFrameBuffer`, and the ScreenCaptureKit implementation.
- A separate external output compiled `videotoolbox_objc` and `native_api` successfully in 1312 build steps with the locked GN arguments.
- `RTCVideoEncoderH264` requests VideoToolbox hardware acceleration, reports `VideoToolbox`, checks `kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder`, and forwards native `RTCCVPixelBuffer` frames without converting them to I420.
- The current WebRTC runtime does not link those SDK targets, so the remaining work is ShareMe integration, not a speculative WebRTC replacement.

The first runtime acceptance must still verify actual hardware activation on this Mac. Source inspection and target compilation do not count as runtime hardware evidence.

---

### Task 1: Hardware Feasibility and Dependency Boundary

**Files:**
- Read: `deps/webrtc.lock.json`
- Read: `scripts/bootstrap_webrtc.py`
- Read: `cmake/FindWebRTC.cmake`
- Read: external WebRTC `sdk/BUILD.gn`, `sdk/objc/components/video_codec/RTCVideoEncoderH264.mm`, and ScreenCaptureKit sources
- External generated output: `out/shareme-screen-feasibility` only

**Interfaces:**
- Produces the accepted WebRTC revision, GN arguments, target names, archive roles, and native-frame integration boundary used by Tasks 2-4.

- [x] **Step 1: Verify the locked dependency and source capabilities**

Run:

```bash
python3 tests/scripts/bootstrap_webrtc_test.py
git -C "$WEBRTC_ROOT/checkout/src" rev-parse HEAD
gn desc "$WEBRTC_ROOT/checkout/src/out/shareme" //sdk:videotoolbox_objc
gn desc "$WEBRTC_ROOT/checkout/src/out/shareme" //sdk:native_api
```

Expected: the lock remains at `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`; both SDK targets exist; no external source changes are present.

- [x] **Step 2: Compile the isolated hardware targets**

Run with the exact lock arguments:

```bash
gn gen "$WEBRTC_ROOT/checkout/src/out/shareme-screen-feasibility" --args='clang_use_unsafe_buffers_plugin=false is_debug=false is_component_build=false rtc_build_examples=false rtc_include_tests=false rtc_use_h264=false use_custom_libcxx=false use_rtti=true'
autoninja -C "$WEBRTC_ROOT/checkout/src/out/shareme-screen-feasibility" videotoolbox_objc native_api
```

Expected: both `libvideotoolbox_objc.a` and `libnative_api.a` are produced without modifying `out/shareme` or the checkout source.

- [x] **Step 3: Freeze the boundary**

Use the existing revision. Do not update WebRTC for Task 1. The ShareMe integration must link the Objective-C factory/native bridge as explicit manifest roles and must pass native `VideoFrameBuffer::Type::kNative` frames to it.

---

### Task 2: Profiles and Native ScreenFrame Contract

**Files:**
- Create: `client/core/include/shareme/core/screen_stream_profile.hpp`
- Create: `client/core/src/screen_stream_profile.cpp`
- Modify: `client/core/CMakeLists.txt`
- Create: `client/rtc/screen/include/shareme/rtc/screen_frame.hpp`
- Create: `client/rtc/screen/include/shareme/rtc/screen_video_source.hpp`
- Create: `client/rtc/screen/src/screen_video_source.cpp`
- Create: `client/rtc/screen/CMakeLists.txt`
- Modify: `client/rtc/CMakeLists.txt`
- Create: `tests/core/screen_stream_profile_test.cpp`
- Modify: `tests/core/CMakeLists.txt`

**Interfaces:**
- `enum class ScreenStreamProfile { standard, quality, cinema }`.
- `struct ScreenStreamProfileBounds { int max_width; int max_height; int max_frames_per_second; }`.
- `struct ScreenDimensions { int width; int height; friend bool operator==(ScreenDimensions, ScreenDimensions) = default; }`.
- `constexpr ScreenStreamProfileBounds screen_stream_profile_bounds(ScreenStreamProfile) noexcept` returns `(1920,1080,60)`, `(2560,1440,60)`, or `(3840,2160,30)`.
- `std::optional<ScreenStreamProfile> parse_screen_stream_profile(std::string_view) noexcept` accepts `standard`, `quality`, and `cinema` only.
- `ScreenDimensions fit_screen_dimensions(int source_width, int source_height, ScreenStreamProfile) noexcept` preserves aspect ratio, never upscales, and fits within the selected bounds.
- `enum class ScreenFrameBacking { native, i420 }`.
- `struct ScreenFrame { webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer; int width; int height; int64_t capture_timestamp_us; ScreenFrameBacking backing; }`.
- `ScreenVideoSource::create(ScreenCaptureConfig)` returns an injected `LocalVideoSource` whose platform backend submits `ScreenFrame` values.

- [ ] **Step 1: Write profile contract tests**

Cover exact bounds, aspect preservation, no unnecessary upscale, invalid dimensions, and profile parsing. For example:

```cpp
REQUIRE(screen_stream_profile_bounds(ScreenStreamProfile::standard) ==
        ScreenStreamProfileBounds{1920, 1080, 60});
REQUIRE(fit_screen_dimensions(3840, 2160, ScreenStreamProfile::quality) ==
        ScreenDimensions{2560, 1440});
REQUIRE(fit_screen_dimensions(1280, 720, ScreenStreamProfile::quality) ==
        ScreenDimensions{1280, 720});
```

- [ ] **Step 2: Run the new profile test and observe failure**

Run:

```bash
cmake --build --preset build-dev --target shareme_screen_stream_profile_test
ctest --preset test-dev -R '^screen_stream_profile$' --output-on-failure
```

Expected: the test target is not yet defined or the profile API is not yet implemented.

- [ ] **Step 3: Implement the profile API in portable core**

Use integer overflow checks before multiplying source dimensions. Return no dimensions for non-positive input. Keep all profile functions independent of Qt, WebRTC, and platform headers.

- [ ] **Step 4: Implement the native frame seam outside core**

Keep `ScreenFrame` dependent on WebRTC only in `client/rtc/screen`. `native` means a WebRTC `VideoFrameBuffer` with `type() == VideoFrameBuffer::Type::kNative`; `i420` is the software fallback. The source adapter must construct a `webrtc::VideoFrame` from the buffer and timestamp without calling `ToI420()` for native frames.

- [ ] **Step 5: Build and run the profile/core tests**

Run:

```bash
cmake --build --preset build-dev
ctest --preset test-dev -R '^(screen_stream_profile|bounded_queue|audio_route|movie_audio_renderer)$' --output-on-failure
```

Expected: all selected tests pass and the portable-core forbidden-header scan remains clean.

- [ ] **Step 6: Commit the bounded contract**

```bash
git add client/core client/rtc/screen client/rtc/CMakeLists.txt tests/core
git commit -m "feat: add screen stream profiles and native frame seam"
```

---

### Task 3: macOS ScreenCaptureKit Source

**Files:**
- Create: `client/rtc/screen/src/macos_screen_capture_source.hpp`
- Create: `client/rtc/screen/src/macos_screen_capture_source.mm`
- Modify: `client/rtc/screen/src/screen_video_source.cpp`
- Modify: `client/rtc/screen/CMakeLists.txt`
- Create: `tests/rtc/macos_screen_capture_source_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- `MacScreenCaptureConfig { ScreenStreamProfile profile; optional<uint32_t> display_id; bool show_cursor; }`.
- `MacScreenCaptureSource::start()` and `stop() noexcept` own `SCStream` and its delegate lifetime.
- The delegate delivers `CVPixelBufferRef` plus capture timestamp into a one-slot latest-frame state; the capture callback never calls WebRTC or Qt.

- [ ] **Step 1: Write bounded callback and shutdown tests**

Use a fake callback queue to verify that a second frame replaces the first, callback delivery occurs outside the capture callback, and a delayed callback after `stop()` is rejected. Assert `frames_captured`, `frames_dropped`, and `latest_pending <= 1`.

- [ ] **Step 2: Run the focused source test and observe failure**

```bash
ctest --preset test-movie-call-dev -R '^macos_screen_capture_source$' --output-on-failure
```

Expected: the new target is not yet present.

- [ ] **Step 3: Implement display selection and profile sizing**

Use `SCShareableContent` to select the requested display or the primary display. Compute dimensions with `fit_screen_dimensions`. Configure `SCStreamConfiguration` with `kCVPixelFormatType_32BGRA`, the selected width/height, cursor policy, and `minimumFrameInterval`. Do not add the legacy CGDisplayStream fallback.

- [ ] **Step 4: Preserve native CVPixelBuffer ownership**

In the ScreenCaptureKit sample-buffer callback, retain the `CVPixelBufferRef` through `RTCCVPixelBuffer`, wrap it in WebRTC `ObjCFrameBuffer`, and create a `ScreenFrame` with `backing = native`. Do not lock the pixel buffer or call `ToI420()` on the primary path.

- [ ] **Step 5: Add the worker handoff**

The callback only replaces `latest_frame_` under a mutex and signals a worker. The worker moves the latest frame out, calls `ScreenVideoSource::OnFrame`, and repeats. Stop sets `accepting_callbacks=false`, wakes the worker, joins it, then releases the stream and delegate. Late Objective-C callbacks must observe the disabled state before touching the owner.

- [ ] **Step 6: Run macOS source tests and compile the adapter**

```bash
cmake --build --preset build-movie-call-dev --target shareme_rtc_demo macos_screen_capture_source_test
ctest --preset test-movie-call-dev -R '^(macos_screen_capture_source|screen_stream_profile)$' --output-on-failure
```

Expected: deterministic queue/shutdown tests pass. Screen permission and live display capture remain runtime acceptance evidence, not unit-test evidence.

- [ ] **Step 7: Commit the macOS capture source**

```bash
git add client/rtc/screen tests/rtc
git commit -m "feat: add bounded macOS screen capture source"
```

---

### Task 4: VideoToolbox H.264 Selection and Software Fallback

**Files:**
- Modify: `deps/webrtc.lock.json`
- Modify: `scripts/bootstrap_webrtc.py`
- Modify: `tests/scripts/bootstrap_webrtc_test.py`
- Modify: `cmake/FindWebRTC.cmake`
- Create: `client/rtc/webrtc/include/shareme/rtc/video_encoder_selection.hpp`
- Create: `client/rtc/webrtc/src/macos_video_encoder_selection.hpp`
- Create: `client/rtc/webrtc/src/macos_video_encoder_selection.mm`
- Modify: `client/rtc/webrtc/src/webrtc_runtime.hpp`
- Modify: `client/rtc/webrtc/src/webrtc_runtime.cpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/include/shareme/rtc/video_codec_report.hpp`
- Create: `tests/rtc/video_encoder_selection_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- `struct VideoEncoderDiagnostics { string requested_codec; string negotiated_codec; string encoder_implementation; bool hardware_active; bool fallback_active; string fallback_reason; }`.
- `struct VideoEncoderSelection { unique_ptr<webrtc::VideoEncoderFactory> factory; VideoEncoderDiagnostics diagnostics; int max_width; int max_height; }`.
- `VideoEncoderSelection select_screen_video_encoder(ScreenStreamProfile profile)` performs a macOS VideoToolbox capability probe before source creation.
- `WebRtcRuntime::create(AudioDevice, VideoEncoderSelection)` installs the selected factory without changing network, audio, signaling, or PeerConnection ownership.

- [ ] **Step 1: Extend the dependency lock and manifest contract tests**

Add `sdk:native_api` and `sdk:videotoolbox_objc` as explicit build targets while retaining the exact revision and GN arguments. Extend manifest archive roles for the native bridge and VideoToolbox dependency archives. The bootstrap must fail if any required archive is missing or if the manifest revision/GN arguments differ from the lock.

- [ ] **Step 2: Run bootstrap contract tests and observe the expected manifest failure**

```bash
python3 -m unittest tests/scripts/bootstrap_webrtc_test.py
```

Expected: tests fail until the expected target and archive-role assertions are updated together.

- [ ] **Step 3: Implement the manifest/library mapping**

Keep the old three-archive ABI roles unchanged for Windows/Linux. Add Darwin-only native bridge archive paths and required `VideoToolbox.framework`, `CoreVideo.framework`, `CoreMedia.framework`, and Foundation linkage. Do not overwrite the old `out/shareme` manifest; build the accepted revision in a versioned screen-streaming output.

- [ ] **Step 4: Implement the macOS hardware probe and factory selection**

Create a small `VTCompressionSession` probe with H.264, the selected profile dimensions, and `kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder=true`. Read `kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder`; only a true value permits `hardware_active=true`. Use `RTCVideoEncoderFactoryH264` through `ObjCToNativeVideoEncoderFactory` for the actual WebRTC factory. Set `encoder_implementation=VideoToolbox` only when that factory is selected.

- [ ] **Step 5: Implement bounded software fallback**

If the probe, factory creation, or encoder initialization fails, select the existing VP8 template factory, set `fallback_active=true`, record a sanitized reason, and cap `max_width/max_height` at `1920x1080`. The fallback is selected before peer start; it must not recreate signaling, audio, voice, or the room.

- [ ] **Step 6: Write encoder-selection tests**

Test that H.264 negotiation alone cannot set `hardware_active`, a false probe selects fallback, fallback caps resolution, and fallback diagnostics preserve voice/signaling ownership. Use fake probe results and factories; do not claim hardware from a mock.

- [ ] **Step 7: Build the isolated WebRTC integration**

Run:

```bash
python3 scripts/bootstrap_webrtc.py --root "$WEBRTC_ROOT" --output-name shareme-screen-feasibility --build
cmake --preset call-dev -DWEBRTC_ROOT="$WEBRTC_ROOT" -DWEBRTC_OUTPUT_NAME=shareme-screen-feasibility
cmake --build --preset build-call-dev
ctest --preset test-call-dev -R '^(video_encoder_selection|signaled_peer|webrtc_loopback)$' --output-on-failure
```

Expected: the native bridge links on macOS, the current VP8 path remains available, and existing WebRTC lifecycle tests remain green.

- [ ] **Step 8: Commit the dependency and encoder seam**

```bash
git add deps scripts/bootstrap_webrtc.py tests/scripts/bootstrap_webrtc_test.py cmake/FindWebRTC.cmake client/rtc/webrtc tests/rtc
git commit -m "feat: select macOS VideoToolbox screen encoder"
```

---

### Task 5: Screen CLI, Primary Mode, Latest Presentation, and Profile Gates

**Files:**
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/movie_video_playout_adapter.hpp`
- Modify: `client/tools/rtc_demo/movie_video_playout_adapter.cpp`
- Modify: `client/tools/rtc_demo/video_preview_adapter.hpp`
- Modify: `client/tools/rtc_demo/video_preview_adapter.cpp`
- Modify: `client/tools/rtc_demo/qml/Main.qml`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `tests/rtc/video_preview_adapter_test.cpp`
- Create: `scripts/run_screen_stream_smoke.py`
- Create: `docs/verification/hardware-screen-streaming.md`

**Interfaces:**
- CLI accepts `--source screen` and `--screen-profile standard|quality|cinema`; `screen` is the default host source on macOS, while `test`, `desktop`, and `movie` remain explicit compatibility modes.
- `RtcDemoController` receives `ScreenStreamProfile`, selects the encoder before creating the peer, and injects `ScreenVideoSource` only for a host screen session.
- `VideoPreviewCounters` adds `remote_callbacks`, `sink_submissions`, `presentation_coalesced`, `presentation_callback_delay_p95`, and `presentation_callback_delay_max`.

- [ ] **Step 1: Add CLI contract tests**

Cover profile parsing, default screen source, viewer acceptance of `--source screen`, rejection of `--screen-profile` with movie/test sources, and validation output that does not start capture. Preserve all existing movie and voice CLI contracts.

- [ ] **Step 2: Implement controller/profile wiring**

Select encoder and effective capture bounds before `SignaledPeer::create`. On macOS, create the ScreenCaptureKit source. On Windows, leave the existing explicit `desktop` source as the compatibility/software fallback and report native screen hardware as environment-dependent. Do not alter audio initialization or signaling relay types.

- [ ] **Step 3: Write the latest-frame presentation regression test**

Submit three frames before draining the Qt queue and assert only the newest frame reaches `QVideoSink`, `pending_callbacks <= 1`, and the coalesced counter increases. Submit a frame after shutdown and assert no sink callback occurs.

- [ ] **Step 4: Replace pending-frame drop with latest-frame replacement**

Store one prepared frame in `VideoPreviewAdapter::State` under a mutex. A producer replaces the pending frame and increments `presentation_coalesced`; it schedules at most one queued Qt drain. The drain presents the newest frame, clears the slot, and schedules at most one follow-up drain if a newer frame arrived. Never enqueue one Qt callback per decoded frame.

- [ ] **Step 5: Measure presentation delay**

Carry the frame capture timestamp into the pending frame. On sink submission, record delay in a bounded histogram/ring sufficient to calculate P95 and max. Keep counters atomic or protected by the adapter mutex. Do not add a telemetry framework.

- [ ] **Step 6: Add compact diagnostics without redesigning the UI**

Show requested codec, negotiated codec, encoder implementation, hardware/fallback state, profile, and the bounded presentation counters in the existing diagnostics area. Keep Movie Mode diagnostics and controls unchanged.

- [ ] **Step 7: Build and run fail-fast gates**

Run in order, stopping on the first failure:

```bash
cmake --build --preset build-call-dev
ctest --preset test-call-dev --output-on-failure
python3 scripts/run_screen_stream_smoke.py --profile standard --duration-seconds 10
python3 scripts/run_screen_stream_smoke.py --profile standard --duration-seconds 30
python3 scripts/run_screen_stream_smoke.py --profile quality --duration-seconds 30
python3 scripts/run_screen_stream_smoke.py --profile cinema --duration-seconds 30
```

The 10-second run must show a correct picture, no corrupt/green frames, actual `hardware_active=true`, stable voice, and bounded queues before any 30-second profile run. The smoke script must record capture, encode, receive, decode, callback, presentation, bitrate, CPU, RSS, and fallback diagnostics.

- [ ] **Step 8: Run the two-minute final stability gate**

Run exactly one two-minute stability test at the highest required profile that passed. Record foreground/background recovery by minimizing and restoring the receiver once; the next displayed frame must be current rather than replayed backlog.

- [ ] **Step 9: Commit the primary screen mode and presentation path**

```bash
git add client/tools/rtc_demo tests/scripts scripts/run_screen_stream_smoke.py docs/verification/hardware-screen-streaming.md
git commit -m "feat: make hardware screen streaming the primary video mode"
```

---

### Task 6: Final Review, Regression Verification, and Handoff

**Files:**
- Modify: `docs/development/current-stage.md`
- Modify: `docs/verification/hardware-screen-streaming.md`
- Modify: `README.md` only for accurate supported-source/profile documentation

**Interfaces:**
- Handoff records exact starting SHA, final SHA, WebRTC revision, GN args, capture path, native frame representation, profile evidence, diagnostics, CPU/RSS, queue evidence, voice/movie/audio-route results, Windows status, and parked findings.

- [ ] **Step 1: Run fresh configure and relevant full suites**

Run:

```bash
python3 scripts/bootstrap_webrtc.py --root "$WEBRTC_ROOT" --output-name shareme-screen-feasibility --build
cmake --preset dev -DWEBRTC_ROOT="$WEBRTC_ROOT" -DWEBRTC_OUTPUT_NAME=shareme-screen-feasibility
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
cmake --preset call-dev -DWEBRTC_ROOT="$WEBRTC_ROOT" -DWEBRTC_OUTPUT_NAME=shareme-screen-feasibility
cmake --build --preset build-call-dev
ctest --preset test-call-dev --output-on-failure
```

- [ ] **Step 2: Run regression gates**

Run the existing voice regression, repeated RTC lifecycle test, Movie Mode test suite, MovieAudioRenderer tests, audio-route tests, bootstrap contract tests, `git diff --check`, and the portable-core forbidden-header scan. Record macOS evidence separately from Windows CI/native evidence.

- [ ] **Step 3: Review the final diff and parked findings**

Confirm no system audio, Movie Stage 2B, HDR, remote input, TURN, Linux hardware, or 4K60 optimization work entered the branch. Park only findings that are outside this stage and explain why they do not block the required profiles.

- [ ] **Step 4: Update the canonical handoff**

State explicitly:

1. whether screen streaming is hardware encoded;
2. whether the macOS primary path avoids mandatory CPU frame readback;
3. whether 1080p60 is production-capable under local/LAN evidence;
4. whether 1440p60 is production-capable;
5. whether 4K30 is production-capable;
6. whether 4K60 is experimental or demonstrated;
7. whether the next stage is Integrated Shared System Audio.

- [ ] **Step 5: Create the final focused commit**

```bash
git add docs/development/current-stage.md docs/verification/hardware-screen-streaming.md README.md
git commit -m "docs: record hardware screen streaming foundation"
```

Do not push or deploy automatically. Preserve the external WebRTC checkout and all versioned outputs.
