# Hardware Screen Streaming Verification

## Scope

- macOS ScreenCaptureKit is the host screen source.
- Windows Desktop Duplication (`IDXGIOutputDuplication`) is the Windows host
  screen source. The adapter reuses the existing DXGI capture implementation
  and supplies bounded, even-sized I420 frames to the screen track.
- The bounded capture and Qt presentation paths keep only the latest frame.
- VideoToolbox H.264 is selected only after probe, parameterized-format, factory,
  encoder-creation, and encoder-initialization checks.
- VP8 fallback uses the standard 1920x1080 capture bound.
- Viewer diagnostics identify receive-only state; they do not infer the remote
  encoder without a signaling field.

## Automated Gates

Run from the worktree:

```bash
python3 tests/scripts/bootstrap_webrtc_test.py
cmake --build build/movie-call-dev -j2
ctest --test-dir build/movie-call-dev --output-on-failure
python3 scripts/run_screen_stream_smoke.py --profile standard --duration-seconds 10
python3 scripts/run_screen_stream_smoke.py --profile standard --duration-seconds 30
python3 scripts/run_screen_stream_smoke.py --profile quality --duration-seconds 30
python3 scripts/run_screen_stream_smoke.py --profile cinema --duration-seconds 30
```

The smoke runner writes sanitized JSONL under `out/hardware-screen-streaming`
by default. It requires a macOS screen-capture permission grant and fails closed
unless the host reports active H.264, bounded queues, nonzero bitrate, and media
delivery on both peers.

### Windows commands

Use the existing locked WebRTC checkout; do not bootstrap or rebuild it. From a
Visual Studio developer shell, configure the Qt/WebRTC-only target and run the
native suite:

```powershell
cmake --fresh --preset call-dev `
  -DWEBRTC_ROOT=D:/Deps/shareme-webrtc `
  -DCMAKE_PREFIX_PATH=H:/QT6.11/6.11.1/msvc2022_64 `
  -DCMAKE_LINKER=D:/Deps/shareme-webrtc/checkout/src/third_party/llvm-build/Release+Asserts/bin/lld-link.exe
cmake --build build/call-dev -j 2
ctest --test-dir build/call-dev --output-on-failure

$env:PATH = 'H:\QT6.11\6.11.1\msvc2022_64\bin;H:\Deps\vcpkg\installed\x64-windows\bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
python scripts/run_screen_stream_smoke.py `
  --demo build/call-dev/shareme_rtc_demo.exe `
  --server-root server --profile standard --duration-seconds 10 `
  --allow-software-fallback `
  --artifact out/windows-current-parity/standard-10s.jsonl
```

`--allow-software-fallback` is an explicit Windows acceptance mode, not the
macOS hardware gate. It requires VP8, a `fallback:` hardware status, nonzero
bitrate and delivered media at no more than the standard 1920x1080 bound.

## Integration Review

- **Verified — macOS arm64 at `77722c3`:** bootstrap contracts passed 16/16;
  `call-dev` built without Movie/FFmpeg and passed 39/39; `movie-call-dev`
  rebuilt and passed 64/64; portable `dev` passed 15/15; Go race tests and vet,
  Sol–Terra workflow 8/8, skill validation, and `git diff --check` passed.
- **Verified — fresh native media gates:** `standard` passed at 10 and 30
  seconds, `quality` passed at 30 seconds, and `cinema` passed at 30 and 120
  seconds. Every run reported H.264, active VideoToolbox encoding, nonzero
  bitrate, and increasing host encode plus viewer receive/decode counters.
- The fresh runs again produced `1470x956`; they do not upgrade the exact
  target-resolution, visual-integrity, live-voice, foreground/background,
  Windows, or physical thermal evidence boundaries below.

## Latest Native Attempt

- **Verified — macOS arm64, final binary:** the documented 10-second and
  30-second `standard` gates, the 30-second `quality` and `cinema` gates, and
  the planned 120-second `cinema` stability gate all negotiated H.264 with
  `hardware_encoder_status=active`, bounded `max_pending=1`, nonzero bitrate,
  and nonzero host encode and viewer decode counters. Final artifacts include:
  `standard-10s-final.jsonl`, `standard-30s-final.jsonl`,
  `quality-30s-final.jsonl`, `cinema-30s-final-validator.jsonl`, and
  `cinema-120s-final.jsonl`.
- The current display produced `1470x956` frames for every profile. The smoke
  runs therefore verify the native path and profile-specific H.264 selection,
  but do not prove exact `1920x1080`, `2560x1440`, or `3840x2160` capture
  behavior.
- Root cause of the earlier zero-output run: the locked WebRTC VideoToolbox
  factory advertised H.264 Level 3.1 (`640c1f`/`42e01f`) for all screen
  profiles. At the screen bounds, `RTCVideoEncoderH264` applied that level and
  VideoToolbox returned `kVTParameterErr` (`-12902`). The ShareMe factory now
  advertises Level 4.2 for `standard` and Level 5.1 for `quality`/`cinema`,
  preserving the H.264 profile and packetization parameters.
- An earlier run also recorded
  `screen-shareable-content-unavailable--3801`; ScreenCaptureKit permission,
  actual target-resolution coverage, visual frame integrity, foreground /
  background recovery, and live voice continuity remain environment-dependent
  or unverified.

## Windows Native Verification

- **Verified - Windows AMD64, MSVC 19.51:** fresh `call-dev` built and passed
  42/42 CTest tests. Fresh `movie-call-dev` built and passed 67/67. The server
  passed `go test ./...` and `go vet ./...`; workflow tests passed 8/8, the
  repository skill validator passed, and bootstrap contracts passed 16/16.
- **Verified - standard Desktop Duplication path:** the final 10-second native
  two-peer run delivered 1920x1080 VP8 with
  `hardware_encoder_status=fallback:platform-unavailable`. The host reported
  391 callbacks/submissions, 362 encoded frames and 1,741,717 bit/s. The viewer
  reported 331 received/decoded frames and 517,873 bit/s. Both peers reported
  continuous bidirectional primary-voice traffic with zero stalled samples;
  the viewer recovered exactly once and submitted 273 frames afterward. The
  artifact is `out/windows-current-parity/standard-10s-integrated.jsonl` and
  remains ignored verification evidence rather than a Git input.
- **Partial - quality and cinema fallback:** both profile requests completed a
  native two-peer run with 1920x1080 VP8 and nonzero bitrate, but the static
  desktop yielded only one media frame in each run. These prove routing and
  bounded fallback configuration, not sustained 1440p60 or 4K30 behavior.
- **Environment-dependent - Go race:** Windows had `CGO_ENABLED=0`; enabling
  cgo with the available `clang-cl` failed in `runtime/cgo` because that driver
  rejects the GCC-style `-dM` and `-fno-stack-protector` flags. This is a local
  toolchain limitation, not a passing race result.
- **Unimplemented on Windows:** platform hardware WebRTC encoding, cursor
  composition, display selection, and quality/cinema output above the standard
  software-fallback bound.

## Evidence Boundaries

- **Verified by automated tests:** profile bounds, native/I420 frame contracts,
  adaptation behavior, bounded queue metrics, error propagation, encoder
  fallback, latest-frame presentation, CLI validation, bootstrap manifest
  selection, profile-level adaptation, smoke validation, all four documented
  native profile gates, the two-minute cinema stability gate, and existing
  movie/voice/audio-route regressions.
- **Environment-dependent:** ScreenCaptureKit permission, actual VideoToolbox
  hardware activation, visual frame integrity, 1080p60/1440p60/4K30 stability,
  foreground/background recovery, and live voice continuity.
- **Windows boundary:** Desktop Duplication capture and VP8 software transport
  are verified on the stated Windows host. Windows hardware encoding and
  sustained quality/cinema cadence remain unimplemented or partial as listed
  above; macOS VideoToolbox results do not verify them.
- **Not part of this stage:** system audio capture, HDR, remote input, TURN,
  Movie Stage 2B correlation, hard resync, Linux hardware encoding, and 4K60.
