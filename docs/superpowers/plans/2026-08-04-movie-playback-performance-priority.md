# Movie Playback Quality-Preserving Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce ShareMe movie-call CPU and memory cost without lowering resolution, cadence, codec quality, color fidelity, audio quality, or viewing behavior.

**Architecture:** Freeze a portable quality contract and software baseline, remove redundant BGRA/I420 and Qt ARGB work, then add isolated platform hardware adapters only where profiles show codec dominance. A separate runner compares three software baselines with three automatic-path candidates and blocks delivery on either performance or quality regression.

**Tech Stack:** C++20, CMake, FFmpeg/libswscale, libyuv, libwebrtc, Qt 6/Qt Multimedia, VideoToolbox, D3D11VA/Media Foundation, Python 3, Go.

## Global Constraints

- This is the highest-priority stage; pause viewer reportability, drift runs, and hard-resync.
- Preserve exact stream dimensions, cadence, aspect ratio, color metadata, existing codec/quality configuration, audio, previews, timeline, seeks, and generations.
- Do not downscale, throttle, lower bitrate/quality, degrade chroma, or intentionally drop additional frames.
- Keep `client/core` free of Qt, FFmpeg, WebRTC, GPU, and OS dependencies.
- Keep movie audio, voice, and video paths independent.
- Use the supplied movie but commit only `<MOVIE_PATH>`.
- Keep raw artifacts, traces, media, logs, build output, local paths, and external libwebrtc cache content out of Git.
- Use TDD, one writer, an ignored worktree, focused commits, and platform-specific evidence labels.

---

### Task 1: Create the isolated stage and RED quality contracts

**Files:**
- Create: `tests/core/video_quality_contract_test.cpp`
- Modify: `tests/core/CMakeLists.txt`
- Create: `tests/scripts/movie_performance_study_test.py`
- Modify: `tests/scripts/CMakeLists.txt`

**Interfaces:**
- Produces tests for `VideoQualityContract`, `VideoQualitySample`, `compare_quality`, runner aggregation, and frozen gates.

- [ ] Create `.worktrees/movie-playback-performance` and branch `codex/movie-playback-performance` from current `main`; verify both worktrees and refs.
- [ ] Write RED tests requiring exact width/height, rational cadence, aspect ratio, color range/space, codec/profile, no additional drops, PSNR >=45 dB, and SSIM >=0.995.
- [ ] Write RED Python tests requiring three sequential software baselines and three auto candidates, explicit output-parent containment, no overwrite, sanitized errors, atomic artifacts, CPU/RSS aggregation, and every gate from the design.
- [ ] Run the focused tests and retain only the expected missing-interface failures.
- [ ] Commit as `test: define quality-preserving performance gates`.

### Task 2: Implement the portable quality contract

**Files:**
- Create: `client/core/include/shareme/core/video_quality_contract.hpp`
- Create: `client/core/src/video_quality_contract.cpp`
- Modify: `client/core/CMakeLists.txt`
- Modify: `tests/core/video_quality_contract_test.cpp`

**Interfaces:**
- Produces `VideoQualityContract` with geometry, rational cadence, aspect ratio, color metadata, codec identity, `minimum_psnr_db=45.0`, `minimum_ssim=0.995`, and baseline drops.
- Produces `QualityComparison compare_quality(const VideoQualityContract&, const VideoQualitySample&) noexcept` with explicit failure flags.

- [ ] Implement overflow-safe rational comparison and reject missing, non-finite, or invalid measurements.
- [ ] Test portrait/odd dimensions, equivalent cadence ratios, metadata mismatch, threshold boundaries, non-finite metrics, and extra drops.
- [ ] Run the focused and full portable-core suites.
- [ ] Commit as `feat: add movie video quality contract`.

### Task 3: Add the performance runner and sanitized artifacts

**Files:**
- Create: `scripts/run_movie_performance_study.py`
- Modify: `tests/scripts/movie_performance_study_test.py`
- Modify: `.gitignore` only if the explicit build root is not covered.

**Interfaces:**
- Consumes allowlisted `PERF_COUNTERS version=1` lines.
- Produces atomic ignored JSONL, `summarize_run(path)`, `compare_runs(baseline,candidate)`, and `gates_pass(report)`.

- [ ] Implement the 180-second scenario: 30-second warmup, 120-second measurement, 30-second finalization.
- [ ] Launch server, host, and viewer; monitor both clients every <=250 ms and sample CPU/RSS once per second without administrator privileges.
- [ ] Require three sequential forced-software baselines and later three sequential auto candidates on an identical environment identity.
- [ ] Parse only allowlisted numeric/enum fields; reject or redact room IDs, paths, tokens, SDP, ICE addresses, and credentials.
- [ ] Preserve original failure categories plus partial-artifact names, refuse paths outside the build parent, and never overwrite artifacts.
- [ ] Run focused Python tests and commit as `feat: add movie performance study runner`.

### Task 4: Instrument the unchanged software path

**Files:**
- Modify: `client/media/playback/include/shareme/media/ffmpeg_media_source.hpp`
- Modify: `client/media/playback/src/ffmpeg_media_source.cpp`
- Modify: `client/rtc/movie/include/shareme/rtc/movie_video_source.hpp`
- Modify: `client/rtc/movie/src/movie_video_source.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`

**Interfaces:**
- Produces `--video-acceleration auto|software`, with software forced during baseline capture.
- Produces once-per-second sanitized decode/offer/callback/submit/drop/conversion/failure/dimension/cadence/path counters only under the runner flag.

- [ ] Write RED CLI tests for valid modes, invalid values, validation-only behavior, and role propagation.
- [ ] Add atomic counters at decode output, WebRTC offer, preview callback/conversion/submission, RTC/audio failure, and hardware fallback boundaries without changing media behavior.
- [ ] Capture geometry, cadence, color metadata, and selected codec/acceleration path without device or media identifiers.
- [ ] Run affected media, RTC, CLI, and signaling tests.
- [ ] Commit as `feat: instrument movie playback workload`.

### Task 5: Capture three software baselines and locate hot stacks

**Files:**
- Create: `docs/verification/movie-playback-performance.md`

**Interfaces:**
- Produces sanitized baseline medians/P95, quality contract, hashes, platform/tool identity, and profile conclusions.

- [ ] Use an optimized build with debug symbols, not Debug, and run the supplied movie three times sequentially with `--video-acceleration software`.
- [ ] Keep power mode, display arrangement, server topology, and background workload stable and record them without local paths.
- [ ] Capture optional macOS `xctrace` Time Profiler/Energy Log outside Git; later capture Windows WPA/ETW separately when available.
- [ ] Quantify application-owned shares for decode, `sws_scale`, BGRA-to-I420, WebRTC codec, `ToI420`, I420-to-ARGB, allocation/copy, and Qt submission; mark unknowns honestly.
- [ ] Commit only aggregate evidence and hashes as `docs: record movie performance baseline`.

### Task 6: Remove the BGRA-to-I420 round trip

**Files:**
- Modify: `client/media/playback/include/shareme/media/media_frame.hpp`
- Modify: `client/media/playback/include/shareme/media/ffmpeg_media_source.hpp`
- Modify: `client/media/playback/src/ffmpeg_media_source.cpp`
- Modify: `client/rtc/movie/src/movie_video_source.cpp`
- Modify: `tests/media/ffmpeg_media_source_test.cpp`
- Modify: `tests/rtc/movie_video_source_test.cpp`

**Interfaces:**
- Produces a planar I420 media frame with owned/ref-counted planes, strides, original geometry, PTS, and color metadata.
- `MovieVideoSource` consumes the planes without ABGR/BGRA conversion.

- [ ] Write RED tests proving exact dimensions, cadence, PTS, aspect ratio, color metadata, and I420 plane/stride validity for 4K and existing fixtures.
- [ ] Configure FFmpeg conversion directly to I420; retain one observable fallback for unsupported formats.
- [ ] Remove the redundant BGRA-to-I420 step without changing RTP timestamp, generation, pause/resume, seek, EOS, or audio behavior.
- [ ] Run FFmpeg, timeline, movie video/audio, and fixture tests plus sampled PSNR/SSIM comparison.
- [ ] Commit as `perf: remove redundant movie color conversion`.

### Task 7: Add a ref-counted Qt YUV preview adapter

**Files:**
- Create: `client/tools/rtc_demo/video_preview_adapter.hpp`
- Create: `client/tools/rtc_demo/video_preview_adapter.cpp`
- Modify: `client/tools/rtc_demo/CMakeLists.txt`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Create: `tests/rtc/video_preview_adapter_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces `VideoPreviewAdapter::submit(const webrtc::VideoFrame&)`.
- Produces `VideoPreviewResult { bool submitted; std::uint32_t rtp_timestamp; PreviewPath path; }` and snapshot counters.

- [ ] Write RED tests for planar YUV mapping and lifetime, Qt-thread submission, existing one-frame-in-flight bound, no additional drops, fallback-copy release, RTP timestamp, null sink, and counters.
- [ ] Wrap ref-counted I420 planes in a Qt-compatible `QVideoFrame` buffer when supported; keep one bounded ARGB-copy fallback with an observable reason.
- [ ] Move conversion/delivery ownership out of `RtcDemoController` without changing host/viewer role routing.
- [ ] Trigger viewer rendered-frame reconciliation only after successful sink submission and cover reportability preconditions in regression tests.
- [ ] Run preview, playout-report, signaled-peer, GUI smoke, and affected CTest targets.
- [ ] Commit as `perf: avoid copied ARGB movie previews`.

### Task 8: Re-profile and implement only evidenced hardware adapters

**Files:**
- Create: `client/media/playback/src/video_acceleration.hpp`
- Create/modify: narrowly scoped macOS VideoToolbox adapter files.
- Create/modify: narrowly scoped Windows D3D11VA/Media Foundation adapter files.
- Modify: `client/media/playback/src/ffmpeg_media_source.cpp`
- Modify: platform CMake files and affected tests.

**Interfaces:**
- Produces `enum class VideoAccelerationMode { automatic, software };` and sanitized selected/fallback path.

- [ ] Re-run one diagnostic after Tasks 6-7 and identify whether decode, encode, or both remain dominant; do not implement an unmeasured adapter.
- [ ] On macOS, add VideoToolbox only for the evidenced boundary; on Windows add D3D11VA/Media Foundation under independent platform guards.
- [ ] Preserve exact geometry, cadence, color metadata, codec quality configuration, timestamps, and audio; fall back to software on unsupported or initialization failure.
- [ ] Test forced software, successful auto selection when available, unsupported hardware, initialization failure, fallback counters, and platform-disabled builds.
- [ ] Commit platform changes separately as `perf: add quality-preserving video acceleration`.

### Task 9: Run candidate performance and quality gates

**Files:**
- Modify: `client/tools/rtc_demo/main.cpp`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `docs/verification/movie-playback-performance.md`

**Interfaces:**
- Produces the three-run software-versus-auto comparison and final gate status.

- [ ] Make `auto` the default only after its fallback and quality tests pass; retain explicit `software` mode.
- [ ] Run three sequential auto candidates under frozen baseline conditions.
- [ ] Require exact geometry/metadata/codec configuration, cadence >=99 percent of baseline, no extra drops, PSNR >=45 dB, SSIM >=0.995, CPU reduction >=30 percent, CPU P95 non-regression, and RSS P95 growth <=10 percent.
- [ ] Human-confirm both previews, audible audio, pause/resume, +60-second seek, and -30-second seek; record human evidence separately.
- [ ] If quality fails, fix it before further optimization. If CPU fails while quality passes, preserve evidence and name the next quality-preserving boundary; never lower quality or tune gates.
- [ ] Commit sanitized evidence as `docs: verify quality-preserving movie performance`.

### Task 10: Full verification, review, and handoff

**Files:**
- Modify: `docs/development/current-stage.md`
- Modify: `docs/verification/movie-playback-performance.md`

- [ ] Run full macOS CTest, `signaled_peer` 20 times, `go test -race ./...`, `go vet ./...`, workflow tests, skill validator, and `git diff --check`.
- [ ] Run affected Windows MSVC/CTest and three-run performance evidence when Windows is available; otherwise label it environment-dependent.
- [ ] Inspect tracked and ignored files; keep raw artifacts ignored and preserve external libwebrtc cache read-only.
- [ ] Complete Sol review for quality preservation, timing, buffer lifetimes, fallbacks, counter validity, and evidence; resolve every Critical/Important finding and rerun affected tests.
- [ ] Update the handoff with exactly one outcome: `verified-performance-and-quality`, `partial-evidence`, or `blocked-on-quality-preserving-boundary`.
- [ ] Push `codex/movie-playback-performance`, verify its remote ref, and leave `main` unchanged for Sol/user merge review.

Do not resume viewer reportability, drift measurement, or hard-resync within this branch.
