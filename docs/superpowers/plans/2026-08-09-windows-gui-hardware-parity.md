# Windows GUI and Hardware Screen Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Verify the complete ShareMe GUI on Windows, add guarded Media Foundation H.264 screen encoding and decoding without lowering quality, and close the stage with repeatable native and two-device screen/voice evidence.

**Architecture:** Preserve Desktop Duplication and the current bounded I420 screen path, prove Windows-only `webrtc::VideoEncoder` and `webrtc::VideoDecoder` adapters in an isolated round trip, then expose them through platform H.264 factories with the existing 1080p VP8 fallback. Separate GUI, codec-feasibility, native-call, performance, and human gates so a failure has one owner and a safe rollback.

**Tech Stack:** C++20, CMake/Ninja, Qt 6/QML, locked libwebrtc, Windows DXGI/D3D11, Media Foundation, Python 3, Go signaling, CTest, PowerShell.

## Global Constraints

- Work from an ignored worktree on `codex/windows-gui-hardware-parity`, forked from the current `origin/main`.
- Execute Windows-native tasks from an x64 Visual Studio developer shell on Windows 11; macOS tests never verify Windows behavior.
- Preserve `client/core` as portable C++20 with no Qt, FFmpeg, libwebrtc, D3D11, Media Foundation, or OS headers.
- Preserve the repository-external libwebrtc checkout and depot-tools trees read-only.
- Do not lower 1920x1080@60 standard, 2560x1440@60 quality, or 3840x2160@30 cinema profile policies.
- Do not reduce bitrate policy, codec quality, color behavior, queue bounds, voice behavior, recovery gates, or existing thresholds.
- Keep movie audio, host voice, and viewer voice lifecycle and evidence separate.
- Keep `max_pending <= 1`; do not change capture or presentation drop policy without a separate measured contract.
- No generated build output, JSONL, screenshots, fixtures, logs, caches, local settings, device identifiers, or room IDs enter Git.
- Media Foundation failure before call startup keeps the existing standard-bounded VP8 fallback; no mid-call codec swap is allowed.
- Stop product integration if the standalone Media Foundation checkpoint cannot encode, decode, preserve geometry/timestamps, and release both adapters cleanly.
- Final status is exactly one of `verified-windows-gui-hardware-parity`, `partial-windows-hardware-evidence`, or `blocked-on-media-foundation-boundary`.
- File sharing, Movie Stage 2B, hard resync, system audio, remote input, TURN/public deployment, HDR claims, D3D11 zero-copy, 4K60, installer/signing, and auto-update are out of scope.

---

## Planned file structure

### Portable and GUI evidence

- `scripts/process_metrics.py`: cross-platform process CPU/RSS sampler and sanitized aggregation.
- `tests/scripts/process_metrics_test.py`: deterministic sampler arithmetic and failure tests.
- `scripts/run_gui_call_smoke.py`: consume the shared sampler; retain existing six-state GUI contract.
- `tests/scripts/gui_call_smoke_test.py`: Windows sampler injection and artifact failure coverage.
- `scripts/run_windows_gui_acceptance.py`: Windows-visible GUI/DPI/action orchestration and checklist artifact.
- `tests/scripts/windows_gui_acceptance_test.py`: command, redaction, and gate tests.

### Encoder boundary

- `client/rtc/webrtc/include/shareme/rtc/video_encoder_selection.hpp`: platform-neutral injectable H.264 selection names.
- `client/rtc/webrtc/src/video_encoder_selection.cpp`: generic five-step selection and VP8 fallback.
- `client/rtc/webrtc/src/macos_video_encoder_selection.mm`: unchanged VideoToolbox behavior under renamed seams.
- `client/rtc/webrtc/src/windows/windows_h264_codecs.hpp`: Windows probe/factory boundary.
- `client/rtc/webrtc/src/windows/windows_h264_codecs.cpp`: Media Foundation startup and paired factories.
- `client/rtc/webrtc/src/windows/media_foundation_h264_encoder.hpp`: internal WebRTC encoder class.
- `client/rtc/webrtc/src/windows/media_foundation_h264_encoder.cpp`: bounded I420-to-NV12, MFT I/O, rate/keyframe/release behavior.
- `client/rtc/webrtc/src/windows/media_foundation_h264_decoder.hpp`: internal WebRTC decoder class.
- `client/rtc/webrtc/src/windows/media_foundation_h264_decoder.cpp`: bounded Annex-B/NV12/I420 decode and release behavior.
- `client/rtc/webrtc/src/windows/h264_bitstream.hpp`: platform-free AVCC/Annex-B parsing helpers local to the WebRTC module.
- `client/rtc/webrtc/src/windows/h264_bitstream.cpp`: bounded conversion and SPS/PPS/IDR classification.
- `tests/rtc/h264_bitstream_test.cpp`: bitstream RED/GREEN tests on every platform.
- `tests/rtc/windows_h264_encoder_test.cpp`: Windows-only native initialization, encode, rate, keyframe, and shutdown tests.
- `tests/rtc/windows_h264_decoder_test.cpp`: Windows-only decode, timestamp, geometry, corruption, and shutdown tests.
- `client/tools/windows_h264_probe/CMakeLists.txt`: standalone probe target.
- `client/tools/windows_h264_probe/main.cpp`: sanitized deterministic codec round-trip checkpoint.

### Native motion and acceptance

- `client/tools/screen_motion_fixture/CMakeLists.txt`: test-only Qt fixture target.
- `client/tools/screen_motion_fixture/main.cpp`: fixture window bootstrap and bounded-duration CLI.
- `client/tools/screen_motion_fixture/qml/Main.qml`: moving bars/text/lines/frame marker.
- `scripts/run_windows_screen_acceptance.py`: fixture, signaling, peers, process/media sampling, comparison, and atomic JSONL.
- `tests/scripts/windows_screen_acceptance_test.py`: profile, continuity, performance, hash, and failure-category contracts.
- `docs/verification/windows-gui-hardware-parity.md`: exact native and human evidence.
- `docs/development/current-stage.md`: final canonical handoff only after the stage outcome is known.
- `README.md`: Windows GUI build/run instructions only after commands are verified.

---

### Task 1: Create the isolated Windows stage and capture the immutable baseline

**Files:**
- Read: `AGENTS.md`
- Read: `.agents/skills/shareme-sol-terra/SKILL.md`
- Read: `.agents/skills/shareme-sol-terra/references/project-contract.md`
- Read: `docs/development/current-stage.md`
- Read: `docs/superpowers/specs/2026-08-09-windows-gui-hardware-parity-design.md`
- Create ignored evidence: `out/windows-gui-hardware-parity/baseline/`

**Interfaces:**
- Consumes: current `origin/main`, prepared Windows Qt and libwebrtc dependencies.
- Produces: clean worktree, exact baseline SHA, toolchain manifest, initial CTest/GUI/VP8 artifacts, and a rollback point.

- [ ] **Step 1: Verify repository and dependency ownership**

```powershell
git fetch origin --prune
git status --short --branch
git worktree list --porcelain
git -C D:/Deps/shareme-webrtc/checkout/src status --short --branch
git -C D:/Deps/shareme-webrtc/depot_tools status --short --branch
```

Expected: the selected base is `origin/main`; the main checkout and both external dependency trees are clean. If any tree is dirty, stop and identify ownership before proceeding.

- [ ] **Step 2: Create the owned worktree**

```powershell
git worktree add .worktrees/windows-gui-hardware-parity -b codex/windows-gui-hardware-parity origin/main
Set-Location .worktrees/windows-gui-hardware-parity
git rev-parse HEAD
```

Expected: a named feature branch at the exact current remote-main SHA.

- [ ] **Step 3: Configure and build the untouched baseline**

```powershell
cmake --fresh --preset call-dev `
  -DWEBRTC_ROOT=D:/Deps/shareme-webrtc `
  -DCMAKE_PREFIX_PATH=H:/QT6.11/6.11.1/msvc2022_64 `
  -DCMAKE_LINKER=D:/Deps/shareme-webrtc/checkout/src/third_party/llvm-build/Release+Asserts/bin/lld-link.exe
cmake --build --preset build-call-dev --config Release
ctest --test-dir build/call-dev -C Release --output-on-failure
```

Expected: record the actual test count and failures; do not copy an older test count into new evidence.

- [ ] **Step 4: Run the existing GUI and standard VP8 baselines**

```powershell
$env:PATH = 'H:\QT6.11\6.11.1\msvc2022_64\bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
python scripts/run_gui_call_smoke.py `
  --demo build/call-dev/client/tools/rtc_demo/shareme_rtc_demo.exe `
  --artifact out/windows-gui-hardware-parity/baseline/gui.json `
  --idle-sample-seconds 10
python scripts/run_screen_stream_smoke.py `
  --demo build/call-dev/client/tools/rtc_demo/shareme_rtc_demo.exe `
  --server-root server --profile standard --duration-seconds 30 `
  --allow-software-fallback `
  --artifact out/windows-gui-hardware-parity/baseline/standard-vp8.jsonl
```

Expected: the current GUI runner may fail on the Unix-only process sampler; preserve that exact failure as Task 2 RED evidence. The standard media run must reproduce VP8 or finalize a truthful failed artifact.

- [ ] **Step 5: Record baseline identity without committing artifacts**

```powershell
git rev-parse HEAD
Get-FileHash build/call-dev/client/tools/rtc_demo/shareme_rtc_demo.exe -Algorithm SHA256
git status --short --ignored
```

Expected: only ignored `build/` and `out/` output; no source changes.

---

### Task 2: Make GUI and process evidence genuinely cross-platform

**Files:**
- Create: `scripts/process_metrics.py`
- Create: `tests/scripts/process_metrics_test.py`
- Modify: `scripts/run_gui_call_smoke.py`
- Modify: `tests/scripts/gui_call_smoke_test.py`
- Modify: `tests/scripts/CMakeLists.txt`

**Interfaces:**
- Produces: `ProcessSampler(pid: int)`, `ProcessSample`, `sample()`, and `summarize_samples()`.
- Consumes later: Task 7 GUI acceptance and Task 8 media performance runner.

- [ ] **Step 1: Write RED arithmetic and failure tests**

```python
class ProcessMetricsTest(unittest.TestCase):
    def test_cpu_uses_elapsed_process_time(self):
        previous = RawProcessTimes(monotonic_ms=1000, process_100ns=500)
        current = RawProcessTimes(monotonic_ms=2000, process_100ns=700)
        self.assertAlmostEqual(
            cpu_percent(previous, current, logical_processors=2), 0.001
        )

    def test_rejects_regressing_samples(self):
        earlier = RawProcessTimes(monotonic_ms=1000, process_100ns=500)
        later = RawProcessTimes(monotonic_ms=2000, process_100ns=700)
        with self.assertRaisesRegex(
            ProcessMetricsError, "process-times-regressed"
        ):
            cpu_percent(later, earlier, logical_processors=2)
```

The exact expected percentage must follow one documented convention and be checked with larger realistic 100-ns values; this small example exists only to show the required API.

- [ ] **Step 2: Run the focused test and verify RED**

```powershell
python -m unittest tests/scripts/process_metrics_test.py
```

Expected: import failure because `scripts/process_metrics.py` does not exist.

- [ ] **Step 3: Implement the shared sampler**

```python
@dataclasses.dataclass(frozen=True)
class ProcessSample:
    monotonic_ms: int
    cpu_percent: float
    rss_bytes: int

class ProcessSampler:
    def __init__(self, pid: int):
        self._backend = create_process_backend(pid, sys.platform)

    def sample(self) -> ProcessSample:
        return self._backend.sample()
```

On Windows bind `OpenProcess`, `GetProcessTimes`, `GetProcessMemoryInfo`, and `CloseHandle` through `ctypes`. Use query/read rights only. Divide process CPU time by elapsed wall time and logical processor count, matching the documented per-machine convention. On Darwin/Linux keep `ps` behind the same interface. Raise stable categories for access denial, early exit, malformed output, missing samples, or tick regression. Never synthesize zero load.

- [ ] **Step 4: Replace the GUI runner's direct `ps` call**

Inject a sampler factory into `sample_idle_process`:

```python
def sample_idle_process(demo, duration_seconds, sampler_factory=ProcessSampler):
    process = launch_idle_demo(demo)
    try:
        sampler = sampler_factory(process.pid)
        return summarize_samples(collect_samples(process, sampler, duration_seconds))
    finally:
        terminate_process(process)
```

Retain atomic artifact finalization and all six existing GUI probes.

- [ ] **Step 5: Run GREEN tests**

```powershell
python -m unittest tests/scripts/process_metrics_test.py
python -m unittest tests/scripts/gui_call_smoke_test.py
ctest --test-dir build/call-dev -C Release -R "gui_call_smoke_contract|gui_qml_contract|rtc_demo_cli_contract" --output-on-failure
```

Expected: all pass; Windows GUI smoke produces nonempty CPU/RSS samples.

- [ ] **Step 6: Commit the cross-platform evidence layer**

```powershell
git add scripts/process_metrics.py scripts/run_gui_call_smoke.py tests/scripts/process_metrics_test.py tests/scripts/gui_call_smoke_test.py tests/scripts/CMakeLists.txt
git diff --cached --check
git commit -m "test: make GUI process evidence cross-platform"
```

---

### Task 3: Generalize H.264 selection without changing macOS behavior

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/video_encoder_selection.hpp`
- Modify: `client/rtc/webrtc/src/video_encoder_selection.cpp`
- Modify: `client/rtc/webrtc/src/macos_video_encoder_selection.mm`
- Modify: `tests/rtc/video_encoder_selection_test.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`

**Interfaces:**
- Produces: `PlatformH264Probe`, `PlatformH264Factory`, `probe_platform_h264_codecs`, and `create_platform_h264_encoder_factory`.
- Preserves: `select_screen_video_encoder(profile, probe, factory)` and existing diagnostics semantics.

- [ ] **Step 1: Write RED platform-neutral selector tests**

```cpp
auto selected = select_screen_video_encoder(
    ScreenStreamProfile::quality,
    [](int width, int height, int fps, std::string &) {
      return width == 2560 && height == 1440 && fps == 60;
    },
    [] { return make_fake_h264_factory("MediaFoundation"); });
REQUIRE(selected.diagnostics.negotiated_codec == "H264");
REQUIRE(selected.diagnostics.hardware_active);
```

Also require deterministic fallback reasons for probe rejection, null factory, missing H.264 format, null encoder, and `InitEncode` failure.

- [ ] **Step 2: Verify RED**

```powershell
cmake --build --preset build-call-dev --config Release --target shareme_video_encoder_selection_test
ctest --test-dir build/call-dev -C Release -R "^video_encoder_selection$" --output-on-failure
```

Expected: compile failure because the platform-neutral seams do not exist.

- [ ] **Step 3: Rename only generic seams**

```cpp
using PlatformH264Probe =
    std::function<bool(int, int, int, std::string &)>;
using PlatformH264Factory =
    std::function<std::unique_ptr<webrtc::VideoEncoderFactory>()>;
```

Move platform naming out of generic fallback categories while retaining `VideoToolbox` as the macOS implementation diagnostic.

- [ ] **Step 4: Adapt macOS with no behavior change**

The Objective-C++ implementation provides the renamed probe/factory and retains the existing Level 4.2/5.1, format adaptation, creation, initialization, and fallback behavior.

- [ ] **Step 5: Run focused and macOS regression tests**

```powershell
ctest --test-dir build/call-dev -C Release -R "video_encoder_selection|signaled_peer|screen_stream_smoke_contract" --output-on-failure
```

On macOS additionally rerun one standard 10-second native hardware smoke. Windows static tests do not replace it.

- [ ] **Step 6: Commit the behavior-preserving seam**

```powershell
git add client/rtc/webrtc/include/shareme/rtc/video_encoder_selection.hpp client/rtc/webrtc/src/video_encoder_selection.cpp client/rtc/webrtc/src/macos_video_encoder_selection.mm tests/rtc/video_encoder_selection_test.cpp tests/rtc/signaled_peer_test.cpp
git diff --cached --check
git commit -m "refactor: generalize platform H264 selection"
```

---

### Task 4: Implement and test bounded H.264 bitstream handling

**Files:**
- Create: `client/rtc/webrtc/src/windows/h264_bitstream.hpp`
- Create: `client/rtc/webrtc/src/windows/h264_bitstream.cpp`
- Create: `tests/rtc/h264_bitstream_test.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `convert_avcc_to_annex_b`, `inspect_annex_b`, and `H264AccessUnitInfo`.
- Consumes later: Media Foundation encoded samples in Task 5.

- [ ] **Step 1: Write RED tests for AVCC, Annex-B, and malformed input**

```cpp
const std::array<std::uint8_t, 13> avcc{
    0, 0, 0, 2, 0x67, 0x01,
    0, 0, 0, 3, 0x65, 0x02, 0x03};
const auto converted = convert_avcc_to_annex_b(avcc, 4, 64);
REQUIRE(converted.has_value());
REQUIRE(inspect_annex_b(*converted).has_sps);
REQUIRE(inspect_annex_b(*converted).has_idr);
```

Reject zero NAL lengths, truncated sizes, overflow, unsupported length fields, and output above the explicit byte limit.

- [ ] **Step 2: Verify RED**

Build the new test target and expect missing-header compilation failure.

- [ ] **Step 3: Implement bounded conversion**

Use checked size arithmetic and one pre-sized output vector. Accept Annex-B input without conversion when the caller can retain the sample; convert AVCC with 3- or 4-byte lengths. Return a typed failure category rather than throwing.

- [ ] **Step 4: Run GREEN tests**

```powershell
ctest --test-dir build/call-dev -C Release -R "^h264_bitstream$" --output-on-failure
```

- [ ] **Step 5: Commit the bitstream boundary**

```powershell
git add client/rtc/webrtc/src/windows/h264_bitstream.hpp client/rtc/webrtc/src/windows/h264_bitstream.cpp client/rtc/webrtc/CMakeLists.txt tests/rtc/h264_bitstream_test.cpp tests/rtc/CMakeLists.txt
git diff --cached --check
git commit -m "feat: add bounded H264 bitstream handling"
```

---

### Task 5: Prove Windows Media Foundation H.264 in an encode/decode round trip

**Files:**
- Create: `client/rtc/webrtc/src/windows/windows_h264_codecs.hpp`
- Create: `client/rtc/webrtc/src/windows/windows_h264_codecs.cpp`
- Create: `client/rtc/webrtc/src/windows/media_foundation_h264_encoder.hpp`
- Create: `client/rtc/webrtc/src/windows/media_foundation_h264_encoder.cpp`
- Create: `client/rtc/webrtc/src/windows/media_foundation_h264_decoder.hpp`
- Create: `client/rtc/webrtc/src/windows/media_foundation_h264_decoder.cpp`
- Create: `tests/rtc/windows_h264_encoder_test.cpp`
- Create: `tests/rtc/windows_h264_decoder_test.cpp`
- Create: `client/tools/windows_h264_probe/CMakeLists.txt`
- Create: `client/tools/windows_h264_probe/main.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Modify: `client/tools/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `probe_windows_media_foundation_h264_codecs(width, height, fps, reason)`, which succeeds only when both transforms initialize.
- Produces: `create_windows_media_foundation_h264_encoder_factory()`.
- Produces: `create_windows_media_foundation_h264_decoder_factory()`.
- Produces: `shareme_windows_h264_probe --profile standard|quality|cinema --frames N --artifact PATH`.
- Consumes: Task 4 bitstream helpers and the locked `webrtc::VideoEncoder` and
  `webrtc::VideoDecoder` ABIs.
- Produces: timestamp-matched Y/U/V PSNR and Y-SSIM using the product bitrate policy.

- [ ] **Step 1: Write Windows-native RED lifecycle tests**

```cpp
auto encoder = create_media_foundation_h264_encoder(environment, format);
REQUIRE(encoder != nullptr);
REQUIRE(encoder->RegisterEncodeCompleteCallback(&collector) == WEBRTC_VIDEO_CODEC_OK);
REQUIRE(encoder->InitEncode(&codec, settings) == WEBRTC_VIDEO_CODEC_OK);
REQUIRE(encoder->Encode(frame, &keyframe_request) == WEBRTC_VIDEO_CODEC_OK);
REQUIRE(collector.wait_for_idr(std::chrono::seconds(5)));
REQUIRE(encoder->Release() == WEBRTC_VIDEO_CODEC_OK);
REQUIRE(!collector.received_after_release());
```

Add separate tests for dynamic `SetRates`, a second keyframe request, monotonic timestamps, invalid dimensions, double release, transform failure injection, and callback deregistration.

Write a paired decoder RED test that configures H.264, feeds SPS/PPS plus IDR
and delta access units, checks exact output geometry and RTP timestamps,
requests a keyframe after corrupt input, and proves zero callbacks after release.

- [ ] **Step 2: Verify RED on Windows**

Build the test target and expect missing implementation symbols. Non-Windows CMake must not register or compile the native test.

- [ ] **Step 3: Implement COM/MF ownership and transform selection**

Use RAII for `CoInitializeEx`, `MFStartup`, `IMFTransform`, `IMFSample`, and `IMFMediaBuffer`. Enumerate hardware transforms with `MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER`; require H.264 output and NV12 input. Record only a sanitized transform category, never a machine-specific symbolic link.

- [ ] **Step 4: Implement bounded I420-to-NV12 input**

Allocate at most the configured frame size plus one encoded-output bound and reuse buffers. Preserve even dimensions, color-range metadata where available, monotonic timestamps, and configured frame duration. No adapter-owned input queue may exceed one pending frame.

- [ ] **Step 5: Implement WebRTC output behavior**

Convert AVCC samples with Task 4 helpers, ensure SPS/PPS and IDR classification, construct `webrtc::EncodedImage`, set H.264 codec-specific information, and invoke callbacks outside the encoder state mutex. `SetRates` updates bitrate and frame rate through supported Media Foundation properties; an unsupported update returns a stable category and retains the last accepted configuration.

- [ ] **Step 6: Implement deterministic release**

Stop accepting frames, drain or cancel bounded MFT work, wait for in-flight callbacks, release COM objects on the owning thread, and ensure no callback occurs after `Release` returns. Repeated `Release` is harmless.

Implement the decoder with the same ownership rules. Convert decoded NV12 to
I420 into one reusable bounded output buffer and invoke `DecodedImageCallback`
outside the decoder state mutex. Do not advertise it unless native
initialization succeeds.

- [ ] **Step 7: Build the standalone probe**

The probe creates deterministic I420 frames with moving luma/chroma blocks, parses every encoded access unit, and atomically writes:

```json
{
  "schema": "windows-h264-probe-v1",
  "status": "verified",
  "profile": "standard",
  "implementation": "MediaFoundation",
  "hardware": true,
  "framesSubmitted": 600,
  "framesEncoded": 600,
  "framesDecoded": 600,
  "decodedGeometryExact": true,
  "matchedQualityFrames": 600,
  "psnrY": 35.0,
  "psnrU": 32.0,
  "psnrV": 32.0,
  "ssimY": 0.95,
  "idrFrames": 2,
  "timestampsMonotonic": true,
  "encoderCallbacksAfterRelease": 0,
  "decoderCallbacksAfterRelease": 0
}
```

No raw bitstream is committed. A generated `.h264` may exist only under ignored `out/` for decoder inspection.

- [ ] **Step 8: Execute the hard checkpoint**

```powershell
build/call-dev/client/tools/windows_h264_probe/shareme_windows_h264_probe.exe --profile standard --frames 600 --artifact out/windows-gui-hardware-parity/probe-standard.json
build/call-dev/client/tools/windows_h264_probe/shareme_windows_h264_probe.exe --profile quality --frames 600 --artifact out/windows-gui-hardware-parity/probe-quality.json
build/call-dev/client/tools/windows_h264_probe/shareme_windows_h264_probe.exe --profile cinema --frames 300 --artifact out/windows-gui-hardware-parity/probe-cinema.json
```

Expected: supported profiles initialize hardware, encode and decode all accepted frames, produce IDR plus SPS/PPS, preserve exact geometry and monotonic timestamps, meet Y-PSNR >= 35 dB, U/V-PSNR >= 32 dB and Y-SSIM >= 0.95 with no missing match, and release both adapters with zero late callbacks. `ffprobe` may add evidence but does not replace the native decoder round trip.

If standard fails, commit only tested helpers and probe diagnostics, set `blocked-on-media-foundation-boundary`, and do not execute Tasks 6-10.

- [ ] **Step 9: Commit the isolated codec checkpoint**

```powershell
git add client/rtc/webrtc/src/windows client/rtc/webrtc/CMakeLists.txt client/tools/windows_h264_probe client/tools/CMakeLists.txt tests/rtc/windows_h264_encoder_test.cpp tests/rtc/windows_h264_decoder_test.cpp tests/rtc/CMakeLists.txt
git diff --cached --check
git commit -m "feat: add isolated Windows Media Foundation H264 codecs"
```

---

### Task 6: Integrate the proven codecs with guarded fallback

**Files:**
- Modify: `client/rtc/webrtc/src/video_encoder_selection.cpp`
- Modify: `client/rtc/webrtc/src/webrtc_runtime.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/app_session_config.hpp`
- Modify: `client/tools/rtc_demo/app_session_config.cpp`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/rtc/app_session_config_test.cpp`
- Modify: `tests/rtc/video_encoder_selection_test.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Consumes: Task 3 platform selector and Task 5 Windows encoder/decoder factories.
- Produces: real Windows diagnostics `H264`, `MediaFoundation`, `active`, or exact `fallback:<reason>`.
- Produces: strict `--screen-encoder auto|software`; GUI defaults to `auto`, and `software` forces standard VP8 for controlled measurement.

- [ ] **Step 1: Write RED product-selection tests**

Require Windows platform delegation, exact standard/quality/cinema bounds, fallback on probe/factory/create/init failures, and receiver diagnostics `receive-only` rather than a local hardware claim.

Require session/CLI tests that accept only `auto` and `software`, default
interactive calls to `auto`, reject `software` with quality/cinema requests,
and report `fallback:explicit-software` plus `VP8Template` for the audit mode.

- [ ] **Step 2: Verify RED**

Run selector, peer, and CLI contracts. Expected: Windows still delegates to the non-Apple `platform-unavailable` stub.

- [ ] **Step 3: Wire the Windows platform factory**

Compile Media Foundation files only under `WIN32` and link `mfplat`, `mfreadwrite`, `mfuuid`, `ole32`, `propsys`, and the codec API libraries required by the implementation. Keep macOS and non-Windows behavior unchanged.

Add the Media Foundation decoder to the existing combined decoder factory
before VP8. H.264 must not be offered on Windows when its decoder probe or
factory is unavailable; negotiation remains VP8 so peers cannot select an
undecodable codec.

Thread the typed encoder mode from `AppSessionConfig` through CLI/controller to
the selector. Do not use an environment variable. The GUI has no quality-
degrading switch and always requests `auto`; only explicit diagnostic CLI can
select `software`.

- [ ] **Step 4: Propagate truthful diagnostics**

The existing details drawer and counter line must show:

```text
webrtc_encoder=H264 hardware_encoder_status=active encoder_implementation=MediaFoundation
```

Fallback retains VP8 and one stable reason. Logs contain no transform symbolic link, device name, local path, room ID, or SDP.

- [ ] **Step 5: Run Windows focused and full suites**

```powershell
cmake --build --preset build-call-dev --config Release
ctest --test-dir build/call-dev -C Release -R "video_encoder_selection|windows_h264_encoder|windows_h264_decoder|signaled_peer|rtc_demo_cli_contract|gui_qml_contract" --output-on-failure
ctest --test-dir build/call-dev -C Release --output-on-failure
```

Rerun macOS selector tests and a standard VideoToolbox smoke because the shared selector changed.

- [ ] **Step 6: Commit guarded integration**

```powershell
git add client/rtc/webrtc/src/video_encoder_selection.cpp client/rtc/webrtc/src/webrtc_runtime.cpp client/rtc/webrtc/CMakeLists.txt client/tools/rtc_demo/app_session_config.hpp client/tools/rtc_demo/app_session_config.cpp client/tools/rtc_demo/main.cpp client/tools/rtc_demo/rtc_demo_controller.hpp client/tools/rtc_demo/rtc_demo_controller.cpp tests/rtc/app_session_config_test.cpp tests/rtc/video_encoder_selection_test.cpp tests/rtc/signaled_peer_test.cpp tests/scripts/rtc_demo_cli_test.py
git diff --cached --check
git commit -m "feat: select Windows hardware screen codecs"
```

---

### Task 7: Add deterministic motion and native Windows GUI acceptance

**Files:**
- Create: `client/tools/screen_motion_fixture/CMakeLists.txt`
- Create: `client/tools/screen_motion_fixture/main.cpp`
- Create: `client/tools/screen_motion_fixture/qml/Main.qml`
- Create: `scripts/run_windows_gui_acceptance.py`
- Create: `tests/scripts/windows_gui_acceptance_test.py`
- Modify: `client/tools/CMakeLists.txt`
- Modify: `tests/scripts/CMakeLists.txt`

**Interfaces:**
- Produces: `shareme_screen_motion_fixture --profile standard|quality|cinema --duration-seconds N`.
- Produces: `windows-gui-acceptance-v1` artifact with DPI, page/action, shutdown, and privacy results.
- Consumes: Task 2 process sampler.

- [ ] **Step 1: Write RED fixture CLI and GUI artifact tests**

Require rejection of invalid profiles/durations, an offscreen fixture smoke, all eight GUI surfaces, six existing automated state/action probes, clean shutdown, nonempty process samples, redaction, and atomic failed artifacts.

- [ ] **Step 2: Implement the test-only motion fixture**

Render full-window color bars, 1-pixel and 2-pixel line patterns, readable text, a moving block, and a monotonically increasing frame/time label. Profile mode selects timer cadence only; window geometry follows the actual display. Expose an accessible name and deterministic offscreen exit marker.

- [ ] **Step 3: Implement visible GUI acceptance orchestration**

Execute automated offscreen probes, then emit a manual checklist for 100%, 125%, 150%, and 200% scaling. Do not infer visual quality from process survival. Record an unavailable DPI mode as `environment-dependent`.

- [ ] **Step 4: Execute Windows GUI acceptance**

At every available scale verify home, create, join, settings, help, call, details, recovery, minimum size, keyboard focus, audio toggles, leave, and return-home. Keep screenshots only under ignored `out/`.

- [ ] **Step 5: Run tests and commit**

```powershell
python -m unittest tests/scripts/windows_gui_acceptance_test.py
ctest --test-dir build/call-dev -C Release -R "gui_call_smoke_contract|gui_qml_contract|windows_gui_acceptance" --output-on-failure
git add client/tools/screen_motion_fixture client/tools/CMakeLists.txt scripts/run_windows_gui_acceptance.py tests/scripts/windows_gui_acceptance_test.py tests/scripts/CMakeLists.txt
git diff --cached --check
git commit -m "test: add Windows GUI and motion acceptance"
```

---

### Task 8: Build the auditable Windows screen/performance runner

**Files:**
- Create: `scripts/run_windows_screen_acceptance.py`
- Create: `tests/scripts/windows_screen_acceptance_test.py`
- Modify: `tests/scripts/CMakeLists.txt`
- Reuse: `scripts/process_metrics.py`
- Reuse: `scripts/run_screen_stream_smoke.py`

**Interfaces:**
- Produces: `windows-screen-acceptance-v1` per-run JSONL and one atomic comparison JSON.
- Consumes: fixture, ShareMe host/viewer, Go server, process sampler, and existing counter parser.

- [ ] **Step 1: Write RED contract tests**

Test profile commands, role ownership, fixture lifecycle, hardware/fallback classification, continuous samples, geometry agreement, cadence, queue/drop, voice, recovery, CPU/RSS aggregation, SHA-256, path protection, interruption, early process exit, and partial artifact preservation.

- [ ] **Step 2: Freeze aggregation rules**

```python
def measurement_window(samples, start_s=30, end_s=150):
    selected = [sample for sample in samples if start_s <= sample.elapsed_s <= end_s]
    if not selected or selected[0].elapsed_s > start_s or selected[-1].elapsed_s < end_s:
        raise AcceptanceError("measurement-window-incomplete")
    return selected

def percentile(values, percentile_value):
    ordered = sorted(values)
    if not ordered:
        raise AcceptanceError("percentile-input-empty")
    index = math.ceil((percentile_value / 100.0) * len(ordered)) - 1
    return ordered[max(0, min(index, len(ordered) - 1))]

def median_of_three(values):
    if len(values) != 3:
        raise AcceptanceError("three-runs-required")
    return statistics.median(values)

def compare_standard(baseline_runs, hardware_runs):
    baseline_cpu = median_of_three([run.host.cpu_mean for run in baseline_runs])
    hardware_cpu = median_of_three([run.host.cpu_mean for run in hardware_runs])
    reduction = (baseline_cpu - hardware_cpu) / baseline_cpu
    return Comparison(cpu_reduction=reduction, accepted=reduction >= 0.30)
```

Reject fewer than three complete runs, missing samples, mixed geometry, counter regression, or a run outside its declared encoder mode.

- [ ] **Step 3: Implement bounded orchestration**

Start fixture, server, host, and viewer in order; poll all processes and outputs at most every 250 ms; terminate the run on an unexpected exit; finalize partial evidence atomically; and stop children in reverse order. Do not store raw command lines or unsanitized stderr.

- [ ] **Step 4: Register and run runner tests**

```powershell
python -m unittest tests/scripts/windows_screen_acceptance_test.py
ctest --test-dir build/call-dev -C Release -R "windows_screen_acceptance|screen_stream_smoke_contract" --output-on-failure
```

- [ ] **Step 5: Commit the runner**

```powershell
git add scripts/run_windows_screen_acceptance.py tests/scripts/windows_screen_acceptance_test.py tests/scripts/CMakeLists.txt
git diff --cached --check
git commit -m "test: gate Windows hardware screen quality"
```

---

### Task 9: Execute software baseline and hardware candidate measurements

**Files:**
- Write ignored evidence: `out/windows-gui-hardware-parity/performance/`
- Modify after evidence: `docs/verification/windows-gui-hardware-parity.md`

**Interfaces:**
- Consumes: Task 8 runner and explicit software/hardware modes.
- Produces: three complete standard VP8 baselines, three complete standard MF H.264 candidates from the same executable SHA, profile gates, hashes, and one comparison.

- [ ] **Step 1: Freeze machine and workload identity**

Record sanitized Windows version, CPU/GPU model class, logical processor count, RAM, display resolution/refresh, AC/battery state, Qt/MSVC versions, executable SHA-256, and Git SHA. Exclude usernames, serial numbers, absolute paths, and room IDs.

Require the baseline and candidate artifacts to record the same executable
SHA-256. The runner passes `--screen-encoder software` for baseline and
`--screen-encoder auto` for candidate and rejects any diagnostic/mode mismatch.

- [ ] **Step 2: Run three sequential software baselines**

```powershell
1..3 | ForEach-Object {
  python scripts/run_windows_screen_acceptance.py --mode software-baseline --profile standard --duration-seconds 180 --port (18200 + $_) --artifact ("out/windows-gui-hardware-parity/performance/vp8-run-{0}.jsonl" -f $_)
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Expected: exact 1920x1080 actual geometry on a compatible display, deterministic motion, complete samples, VP8 fallback classification, and no media/voice/recovery failure.

- [ ] **Step 3: Run three sequential hardware candidates**

```powershell
1..3 | ForEach-Object {
  python scripts/run_windows_screen_acceptance.py --mode hardware --profile standard --duration-seconds 180 --port (18300 + $_) --artifact ("out/windows-gui-hardware-parity/performance/mf-run-{0}.jsonl" -f $_)
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
```

Expected: H.264, MediaFoundation, active hardware, the same geometry/cadence policy, and all media/voice/recovery gates pass.

- [ ] **Step 4: Evaluate the frozen standard comparison**

Accept only if median hardware host CPU mean improves by at least 30%, host CPU P95 does not regress, host RSS P95 growth is at most 15%, cadence is at least 95%, and no quality gate fails. Missing evidence is failure.

- [ ] **Step 5: Run quality and cinema native gates**

```powershell
python scripts/run_windows_screen_acceptance.py --mode hardware --profile quality --duration-seconds 120 --port 18401 --artifact out/windows-gui-hardware-parity/performance/quality-120s.jsonl
python scripts/run_windows_screen_acceptance.py --mode hardware --profile cinema --duration-seconds 120 --port 18402 --artifact out/windows-gui-hardware-parity/performance/cinema-120s.jsonl
```

Require 2560x1440@60 and 3840x2160@30 policy respectively, at least 95% submitted cadence under motion, Media Foundation H.264 active, and every common gate. If the physical display cannot supply a profile, label it environment-dependent.

- [ ] **Step 6: Record manual thermal observation separately**

After equal ambient and warm-up conditions, record charging state, power mode, call duration, subjective chassis heat, and fan behavior for baseline and hardware. Do not infer temperature from CPU.

- [ ] **Step 7: Stop or proceed based on evidence**

If H.264 or quality fails, preserve branch/artifacts, document `partial-windows-hardware-evidence` or `blocked-on-media-foundation-boundary`, and do not lower gates. Proceed only when the relevant automated hardware and standard performance gates pass; unavailable physical-display profiles remain explicit boundaries.

---

### Task 10: Perform two-device human screen and voice acceptance

**Files:**
- Modify: `docs/verification/windows-gui-hardware-parity.md`
- Write ignored checklist source: `out/windows-gui-hardware-parity/two-device/`

**Interfaces:**
- Consumes: hardware-passing Windows binaries and two physical Windows devices.
- Produces: sanitized human visual, acoustic, recovery, and thermal evidence.

- [ ] **Step 1: Verify binary and repository identity on both devices**

Both devices use the same Git SHA and executable SHA-256. Record network type without SSID/IP. Confirm no local source changes and no encoder fallback.

- [ ] **Step 2: Execute the GUI room flow**

Create a room on the host GUI, join from the viewer GUI, confirm local host preview and remote viewer video, open details, and verify H.264, MediaFoundation, and active hardware on the host path.

- [ ] **Step 3: Execute the visual checklist**

Display the motion fixture and inspect color bars, thin lines, gradients, readable text, moving block, cursor, aspect ratio, and absence of persistent green/black frames. Repeat minimize/restore and one safe display sleep/wake or lock/unlock cycle where policy permits.

- [ ] **Step 4: Execute the acoustic checklist**

Speak in both directions, verify audible remote voice, mute/unmute, speaker off/on, acceptable echo, and continuity while the screen moves. Use headsets when needed to distinguish routing correctness from acoustic echo, and record the condition.

- [ ] **Step 5: Execute lifecycle and heat checks**

Verify leave/rejoin, host termination, viewer recovery message, clean shutdown, a 20-minute standard call, and a 10-minute supported quality/cinema call. Record subjective heat/fan observations with power/charging context.

- [ ] **Step 6: Classify human evidence**

Every item is `verified`, `failed`, `environment-dependent`, or `not-run`. Do not average missing or failed items into a pass.

---

### Task 11: Final review, documentation, integration, and cleanup

**Files:**
- Create or finalize: `docs/verification/windows-gui-hardware-parity.md`
- Modify: `docs/development/current-stage.md`
- Modify: `README.md`
- Review: every file changed from `origin/main...HEAD`

**Interfaces:**
- Consumes: all artifacts, native results, human checklist, and exact Git/binary hashes.
- Produces: one truthful stage outcome, focused commits, pushed branch, and merge decision.

- [ ] **Step 1: Run complete Windows regression**

```powershell
cmake --build --preset build-call-dev --config Release
ctest --test-dir build/call-dev -C Release --output-on-failure
cmake --fresh --preset movie-call-dev `
  -DWEBRTC_ROOT=D:/Deps/shareme-webrtc `
  -DCMAKE_PREFIX_PATH=H:/QT6.11/6.11.1/msvc2022_64 `
  -DCMAKE_LINKER=D:/Deps/shareme-webrtc/checkout/src/third_party/llvm-build/Release+Asserts/bin/lld-link.exe
cmake --build --preset build-movie-call-dev --config Release
ctest --test-dir build/movie-call-dev -C Release --output-on-failure
Set-Location server
go test ./...
go vet ./...
Set-Location ..
python -m unittest discover -s tests/workflow -p "*_test.py"
python scripts/validate_shareme_skill.py
git diff --check
```

Run `signaled_peer` 20 consecutive times. Record Go race as verified only if a working Windows cgo toolchain actually executes `go test -race ./...`.

- [ ] **Step 2: Rerun affected macOS regression**

Because encoder selection is shared, rerun macOS `call-dev`, `movie-call-dev`, GUI smoke, standard 10-second VideoToolbox native smoke, Go race/vet, workflow, skill validation, and `git diff --check`. macOS success proves no regression only; it does not replace Windows results.

- [ ] **Step 3: Perform final code and evidence review**

Review COM ownership, callback-after-release safety, thread affinity, buffer bounds, timestamp units, Annex-B conversion, SPS/PPS/IDR behavior, dynamic rates, fallback truthfulness, media isolation, redaction, artifact atomicity, quality gates, and generated-file exclusion. Fix every Critical and Important finding and rerun affected evidence.

- [ ] **Step 4: Write the exact outcome**

The verification document contains exact platform, test counts, durations, dimensions, cadence, codec/implementation/status, CPU/RSS comparison, voice/recovery counters, hashes, and human checklist. Set exactly one approved outcome and list every remaining boundary.

- [ ] **Step 5: Update dynamic handoff and README**

Update `current-stage.md` only with results verified against source, Git, and artifacts. Add Windows GUI run instructions to README only if executed successfully. Packaging remains the following stage.

- [ ] **Step 6: Commit documentation**

```powershell
git add docs/verification/windows-gui-hardware-parity.md docs/development/current-stage.md README.md
git diff --cached --check
git commit -m "docs: record Windows GUI hardware parity"
```

- [ ] **Step 7: Verify branch delivery readiness**

```powershell
git status --short --branch
git log --oneline origin/main..HEAD
git diff --stat origin/main...HEAD
git diff --check origin/main...HEAD
git -C D:/Deps/shareme-webrtc/checkout/src status --short --branch
git -C D:/Deps/shareme-webrtc/depot_tools status --short --branch
```

Expected: feature tree clean; only intentional source/tests/docs differ; external dependency trees remain clean.

- [ ] **Step 8: Push and integrate only when the outcome permits**

Push the feature branch and verify its remote SHA. Merge only for `verified-windows-gui-hardware-parity` or an explicitly approved `partial-windows-hardware-evidence` whose code path is safe, fallback-correct, and fully regression-tested. Never merge failed product integration under `blocked-on-media-foundation-boundary`; a probe-only diagnostic slice may receive a separate review.

- [ ] **Step 9: Verify merged result and clean only the owned worktree**

After merge, rerun the required Windows suite on merged `main`, push `main`, verify `origin/main`, inspect ignored/tracked state, remove only `.worktrees/windows-gui-hardware-parity`, prune registrations, and delete only the merged local feature branch. Preserve the remote feature branch as stage backup unless explicitly asked to delete it.
