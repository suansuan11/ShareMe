# Windows Desktop Duplication Capture Design

## Goal

Replace the Windows host's synthetic video source on demand with a real primary
display source backed by `IDXGIOutputDuplication`. The source must feed the
existing `LocalVideoSource` and `SignaledPeer` path, sustain 1080p at up to
60 fps when the desktop produces frames, and preserve the existing macOS
build and runtime behavior.

## Scope

This slice includes Windows primary-display capture, D3D11 texture acquisition,
WebRTC I420 frame delivery, stable capture errors, local hardware smoke tests,
and a `--source desktop` option in the existing RTC demo.

It excludes multi-monitor selection UI, cursor-shape composition, HDR/tone
mapping, protected-content bypass, hardware H264, native GPU texture encoding,
system audio, and production room controls.

## Chosen approach

The source uses Desktop Duplication on a D3D11 hardware device. For every frame
reported by `AcquireNextFrame`, it copies the acquired GPU texture into one
persistent CPU-readable staging texture, maps that texture, and converts its
BGRA rows directly into a WebRTC I420 buffer with libyuv. There is no GDI,
`BitBlt`, periodic screenshot timer, or intermediate full-frame CPU bitmap.

This is one GPU-to-CPU readback per delivered frame. A custom WebRTC native
texture buffer is intentionally deferred: the locked build currently uses the
VP8 software encoder and would request I420 anyway, so a native handle would
add lifetime and adapter complexity without removing the readback.

## Components

`client/rtc/desktop` is a Windows-only target. It owns all D3D11 and DXGI types
and links `d3d11`, `dxgi`, and the existing WebRTC adapter. No Windows header is
exposed through portable targets.

`DesktopFrameConverter` accepts a mapped BGRA view plus dimensions and row
pitch, validates checked sizes, and writes a `webrtc::I420Buffer`. Keeping this
operation separate provides deterministic color, stride, and invalid-input
tests without requiring a desktop session.

`DesktopCaptureSource` derives from `LocalVideoSource`. It owns one capture
thread, D3D11 device/context, selected output, duplication object, and staging
texture. `start()` completes device/output initialization before returning;
`stop()` requests interruption and joins the thread. The source reports frame
and drop counters, state, and one stable sanitized error category.

The RTC demo keeps synthetic video as the portable default. A Windows host can
select `--source desktop`; the controller then supplies a
`LocalVideoSourceFactory` and uses `SignaledVideoMode::injected`. A viewer or a
non-Windows build rejects desktop source selection at the CLI boundary.

## Capture flow

1. Enumerate DXGI adapters and attached outputs and select the primary output
   containing desktop coordinate `(0, 0)`; fall back to the first attached
   output only if Windows reports no primary coordinate match.
2. Create a D3D11 hardware device for that adapter with BGRA support and call
   `DuplicateOutput`.
3. Block in `AcquireNextFrame` with a bounded timeout. A timeout means the
   desktop has not changed and produces no duplicate WebRTC frame.
4. Query the acquired resource for `ID3D11Texture2D`, recreate the persistent
   staging texture only when format or dimensions change, copy and map it.
5. Call `AdaptFrame`. Convert BGRA directly to the requested I420 size; use an
   I420 crop/scale only when WebRTC adaptation requests crop or scale.
6. Build a frame with monotonic microsecond and 90 kHz RTP timestamps, apply
   the DXGI output rotation, call `OnFrame`, unmap, and release the duplicated
   frame on every exit path.

The source is screencast content and disables denoising. Frame delivery is
bounded by native desktop updates and a 60-fps ceiling; it never uses a
screenshot timer to synthesize cadence.

## Failure handling

Initialization failures are categorized as `desktop-device-unavailable`,
`desktop-output-unavailable`, or `desktop-duplication-unavailable`.
Unsupported texture data is `desktop-frame-unsupported`; conversion or checked
size failures are `desktop-frame-invalid`.

`DXGI_ERROR_WAIT_TIMEOUT` is normal. `DXGI_ERROR_ACCESS_LOST` triggers a bounded
duplication rebuild on the same output, covering display mode changes, lock
screen transitions, and GPU resets. If rebuild fails, the source reports
`desktop-access-lost` and stops. `stop()` remains prompt even while no frames
arrive because acquisition uses a bounded timeout.

## Cross-platform boundary

The desktop target and tests are added only under `WIN32`. The demo links it
and defines desktop-source support only when the target exists. Existing
Apple Objective-C++ microphone permission sources, macOS bundles, FFmpeg movie
sources, WebRTC dependency discovery, and locked dependency bootstrap are not
modified.

## Verification

Deterministic tests cover BGRA conversion, padded row pitch, invalid dimensions,
and stable source configuration. A Windows hardware smoke captures at least one
real primary-display frame, verifies positive dimensions and increasing frame
timestamps, and requires bounded shutdown. The smoke can return CTest skip code
77 when no interactive attached output exists, but the development Windows
machine must produce a real frame before delivery.

The complete call-only 14-test matrix, complete movie 36-test matrix, Go tests,
and two-process synthetic signaling smoke remain regression gates. The final
manual acceptance runs a desktop host and an independent viewer through the
existing local signaling service and verifies received frames at the captured
desktop dimensions.
