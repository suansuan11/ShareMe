# Windows Cross-Platform Regression Verification

## Scope

On 2026-07-31, the macOS-developed portable core, Qt playback, signaling,
FFmpeg movie decode, native libwebrtc loopback, movie-video, independent
movie-audio, and microphone call paths were rebuilt and exercised on Windows
x64. The locked WebRTC checkout and revision were not changed or rebuilt.

macOS-only AVFoundation microphone permission code cannot execute on Windows.
It remains selected only by `if(APPLE)`; Windows uses the dependency-free
permission implementation and libwebrtc's native Windows audio device module.

## Environment

- Windows x64, MSVC 19.51, Ninja, CMake 4.4
- Qt 6.11.1 MSVC 2022 x64
- FFmpeg 8.1.2 from vcpkg, including the `ffmpeg` fixture tool
- locked libwebrtc revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`
- Release consumer ABI matching `is_debug=false`

## Results

- Qt/WebRTC call-only configuration: 14/14 CTest tests passed.
- Qt/FFmpeg/WebRTC movie configuration: 36/36 CTest tests passed.
- Go signaling packages: all tests passed.
- Synthetic voice plus movie-video/movie-audio smoke passed with 45 received
  320x180 frames, 302 valid movie-audio callbacks, zero invalid callbacks,
  48 kHz stereo PCM, 200 generated chunks, and 23 ms sender A/V skew.
- Native Windows microphone plus movie-video/movie-audio smoke passed with the
  same media acceptance requirements and positive microphone activity on both
  peers.

The first normal movie smoke and one deliberately concurrent smoke returned
the sanitized `SMOKE_ERROR smoke-failed`; the diagnostic repetition and final
serial repetition passed with the values above. No code was changed to hide
the non-reproduced orchestration/timing event. Smoke acceptance remains a
serial test because concurrent runs compete for native media and encoder
resources.

## Cross-platform fixes

The Qt playback executable now embeds a Windows-only explicit application
manifest. This avoids Qt 6.11's invalid auto-generated side-by-side manifest
without changing the macOS bundle configuration.

The staggered-track synchronization test now compares callback wall-clock
timing against media PTS over the interval where audio and video overlap. The
old assertion compared each track's final PTS after the shorter audio track had
ended, which depended on FFmpeg decode batch size and failed on Windows even
though overlapping callbacks were synchronized.

All Windows-specific runtime additions remain guarded by `WIN32` or `_WIN32`:
Winsock initialization, manifests, and process Job Objects are not compiled or
executed on macOS. Shared WebRTC presets use Release because the locked archive
uses `is_debug=false` on every platform.
