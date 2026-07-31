# Windows Desktop Duplication Verification

## Implemented path

The Windows sender now supports an explicit Desktop Duplication video source:

```text
IDXGIOutputDuplication
  -> D3D11 desktop texture
  -> persistent staging texture and one Map
  -> direct BGRA8/RGBA16F to I420 conversion
  -> WebRTC VideoTrack
  -> SignaledPeer / Qt receiver
```

The source selects the primary attached output, uses a bounded 50 ms
`AcquireNextFrame` wait, caps delivery at 60 frames per second, and performs
one rebuild after `DXGI_ERROR_ACCESS_LOST`. It does not use GDI, BitBlt, or a
timer-driven screenshot loop.

Windows HDR desktops can expose `DXGI_FORMAT_R16G16B16A16_FLOAT` even when
`DuplicateOutput1` requests BGRA8. That format is converted directly from the
mapped texture to I420 without allocating an intermediate CPU RGBA frame.
BGRA8 uses libyuv. The current software VP8 encoder requires I420, so a native
GPU-texture WebRTC buffer would not remove the readback until a compatible
hardware encoder path is added.

All source files and D3D11/DXGI link dependencies are selected only under
`WIN32`. macOS does not compile the desktop module, and the default portable
test-pattern source remains unchanged.

## Windows verification

Verified on 2026-07-31 with MSVC 19.51, Qt 6.11.1, and locked libwebrtc
revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`:

- call-only configuration: 16/16 CTest tests passed;
- Qt/FFmpeg/WebRTC movie configuration: 38/38 CTest tests passed;
- Go signaling packages: all tests passed;
- real Desktop Duplication hardware test passed, including at least 30
  delivered 4K HDR frames during a one-second animated interval and bounded
  shutdown;
- local two-process signaling smoke completed Offer, Answer, trickled ICE,
  bidirectional audio RTP, and remote desktop video delivery;
- the viewer received 22 real `3840x2160` HDR desktop frames during the final
  acceptance interval; both peers selected a host ICE candidate.

This proves the minimal local end-to-end desktop path. It does not yet prove
60 fps under sustained 1080p motion, two-physical-machine performance,
hardware encoding, cursor composition, multi-monitor selection, or HDR tone
mapping quality. Those remain performance and quality follow-ups rather than
requirements hidden by this acceptance result.

## Run the Qt sender and receiver

Build the call configuration from a Visual Studio developer shell, using the
already prepared WebRTC archive:

```powershell
cmake --preset call-dev `
  -DWEBRTC_ROOT=D:/Deps/shareme-webrtc `
  -DCMAKE_PREFIX_PATH=H:/QT6.11/6.11.1/msvc2022_64 `
  -DCMAKE_LINKER=D:/Deps/shareme-webrtc/checkout/src/third_party/llvm-build/Release+Asserts/bin/lld-link.exe
cmake --build --preset build-call-dev
$env:PATH = "H:\QT6.11\6.11.1\msvc2022_64\bin;$env:PATH"
```

Start signaling in terminal 1:

```powershell
$env:SHAREME_SIGNALING_ADDR = "127.0.0.1:18080"
Set-Location server
go run ./cmd/signaling
```

Start the desktop sender in terminal 2 and copy its printed room ID:

```powershell
build/call-dev/shareme_rtc_demo.exe `
  --server ws://127.0.0.1:18080/v1/ws `
  --role host `
  --source desktop
```

Start the receiver in terminal 3:

```powershell
build/call-dev/shareme_rtc_demo.exe `
  --server ws://127.0.0.1:18080/v1/ws `
  --role viewer `
  --room ABCDEF
```

For a repeatable headless acceptance run:

```powershell
python scripts/run_signaled_call_smoke.py `
  --probe build/call-dev/shareme_signaled_call_probe.exe `
  --server-root server `
  --video desktop
```
