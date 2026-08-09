# Windows GUI and Hardware Screen Parity Design

Date: 2026-08-09
Status: approved for implementation planning

## Objective

Make the completed ShareMe GUI a verified Windows product surface and close the
largest remaining Windows media gap: quality-preserving H.264 hardware screen
encoding and decoding through Media Foundation. The stage ends with auditable
one-machine automation and a separate two-device human screen-and-voice
acceptance record.

The stage must not lower capture bounds, frame-rate policy, bitrate policy,
codec quality, voice behavior, or existing media gates to obtain a pass.

## Current verified baseline

- `main` at planning time is `85c845a` and is clean and synchronized with
  `origin/main`.
- The complete GUI is verified on macOS, but its Windows native execution is
  unverified.
- Windows Desktop Duplication capture and a local 1920x1080 VP8 two-peer call
  are verified on the previously recorded Windows machine.
- Windows quality and cinema requests currently fall back to the standard
  1920x1080 VP8 bound. Sustained quality/cinema cadence is not established.
- The locked libwebrtc checkout contains no ready-to-enable Windows Media
  Foundation video codec factory and was built with `rtc_use_h264=false`.
  Windows therefore has neither an H.264 encoder nor decoder in the active
  factory. ShareMe needs isolated platform adapters rather than a build flag.
- Desktop capture currently maps a D3D11 staging texture and converts BGRA8 or
  RGBA16F to I420 before WebRTC encoding. A zero-copy D3D11 capture-to-encoder
  path is not part of this stage.

## Considered approaches

### A. Directly replace VP8 with in-call Media Foundation codecs

This is the shortest code path but couples COM/MF startup, H.264 bitstream
format, WebRTC callbacks, rate control, and shutdown to the live call before
the platform boundary is proven. Failures would be hard to distinguish from
capture, signaling, or GUI failures. Rejected.

### B. Checkpointed platform adapter, then guarded product integration

First make GUI and measurement tooling Windows-native. Next prove a standalone
Media Foundation encoder/decoder round trip against deterministic I420 frames.
Only after that gate passes, expose it through platform-H.264 factories and run
real two-peer screen calls. This keeps VP8 as a truthful fallback and produces
an explicit blocked outcome if the locked ABI or machine cannot support the
hardware path. Recommended and selected.

### C. Keep VP8 and perform only GUI/two-device acceptance

This would verify usability but leave quality/cinema capped at the standard
fallback and would not address Windows heat or high-quality streaming. Rejected
as the next product stage, though VP8 remains the safe fallback.

## Architecture

The stage is split into four independently reviewable layers:

1. **Windows GUI and evidence baseline.** Make the existing GUI smoke runner
   collect Windows process evidence without Unix `ps`; verify native visible
   navigation, controls, settings, recovery, DPI behavior, and clean shutdown.
2. **Media Foundation feasibility boundary.** Add Windows-only H.264 encoder
   and decoder adapters implementing the locked `webrtc::VideoEncoder` and
   `webrtc::VideoDecoder` contracts. Exercise an encode/decode round trip from
   a standalone probe before either adapter is reachable from a call.
3. **Guarded WebRTC integration.** Generalize the current macOS-specific
   platform encoder selection seam, retain VideoToolbox behavior unchanged,
   and select Media Foundation only after probe, factory, encoder creation, and
   `InitEncode` all succeed. Otherwise retain the existing standard VP8 path.
4. **Native acceptance.** Run deterministic moving-screen calls for standard,
   quality, and cinema profiles; compare standard hardware H.264 against the
   existing same-geometry VP8 fallback; then perform a two-device human
   screen-and-voice checklist.

Platform code stays outside `client/core`. No Media Foundation, D3D11, Windows,
Qt, FFmpeg, or libwebrtc header may enter the portable core.

## Components and interfaces

### Portable process evidence

`scripts/process_metrics.py` provides one sanitized sampler used by GUI and
Windows acceptance runners:

```python
@dataclass(frozen=True)
class ProcessSample:
    monotonic_ms: int
    cpu_percent: float
    rss_bytes: int

def sample_process(pid: int, interval_seconds: float) -> ProcessSample
```

Darwin/Linux use `ps`; Windows uses `ctypes` bindings to `GetProcessTimes`,
`GetProcessMemoryInfo`, and monotonic wall time. Missing or regressing samples are
errors, never zero-filled evidence. Artifacts contain numeric metrics and
categories only, not command lines, usernames, paths, room IDs, or device IDs.

### Media Foundation H.264 encoder

Windows-only files under `client/rtc/webrtc/src/windows/` own COM and Media
Foundation. The adapter implements the locked libwebrtc interface and accepts
I420 frames from the existing screen source. It converts I420 to one bounded,
reused NV12 input buffer, submits samples to an asynchronous-or-synchronous
Media Foundation Transform chosen by the platform, and emits Annex-B H.264
frames through `EncodedImageCallback`.

The initial adapter deliberately does not claim zero-copy. Its owned buffers
are bounded and reported. D3D11 native-texture input is a later optimization
only after this quality-preserving hardware stage passes.

Required behavior:

- H.264 constrained-baseline or baseline profile compatible with WebRTC;
- packetization-mode 1;
- standard uses H.264 Level 4.2; quality and cinema use Level 5.1;
- `InitEncode`, `RegisterEncodeCompleteCallback`, `Encode`, `SetRates`,
  `Release`, and `GetEncoderInfo` obey the locked WebRTC contract;
- keyframe requests produce an IDR frame;
- SPS/PPS precede or accompany the first IDR after initialization/restart;
- AVCC output, if produced by the transform, is converted to Annex-B without
  unbounded allocation;
- input timestamps and RTP timestamps remain monotonic;
- width, height, cadence, and color-range metadata are preserved;
- shutdown drains or cancels bounded work and never invokes callbacks after
  `Release` returns;
- device/transform failure produces a stable sanitized category and permits
  selector fallback before call startup. Mid-call encoder failure is surfaced
  as a call error; it does not silently change codec inside an SDP session.

### Media Foundation H.264 decoder

The paired Windows decoder consumes Annex-B access units from WebRTC, uses a
hardware Media Foundation H.264 decoder transform when available, and emits
I420 frames with the original RTP timestamp through `DecodedImageCallback`.
It owns at most one pending access unit and one reusable NV12 output buffer.
Resolution changes are accepted only after an IDR/SPS/PPS boundary; malformed
access units fail with a stable category. `Release` waits for bounded in-flight
work and guarantees no callback afterward.

The standalone checkpoint feeds encoder output into this decoder and proves
exact geometry, monotonic timestamps, nonzero decoded frames, bounded buffers,
and deterministic release. It matches decoded frames to deterministic source
timestamps and requires Y-plane PSNR >= 35 dB, U/V PSNR >= 32 dB, and Y-plane
SSIM >= 0.95. Missing quality samples fail the checkpoint. A successful
encoder-only probe is insufficient.

### Platform H.264 selection

Replace VideoToolbox-named generic seams with platform-neutral encoder
interfaces and add the corresponding decoder factory seam:

```cpp
using PlatformH264Probe =
    std::function<bool(int width, int height, int fps, std::string &reason)>;
using PlatformH264Factory =
    std::function<std::unique_ptr<webrtc::VideoEncoderFactory>()>;

VideoEncoderSelection select_screen_video_encoder(
    core::ScreenStreamProfile profile,
    PlatformH264Probe probe = {},
    PlatformH264Factory factory = {});

std::unique_ptr<webrtc::VideoDecoderFactory>
create_platform_video_decoder_factory();
```

The selector remains deterministic and injectable in portable tests. macOS
delegates to VideoToolbox; Windows delegates to Media Foundation; other
platforms report `platform-unavailable`. Diagnostics use stable implementation
names `VideoToolbox`, `MediaFoundation`, and `VP8Template`.

Selection is accepted only when all of these pass:

1. profile capability probe;
2. factory creation;
3. advertised H.264 format validation;
4. encoder creation;
5. profile-sized `InitEncode` and `Release` probe.

Any pre-call failure retains the existing standard-bounded VP8 fallback and
records one exact reason. The UI details drawer displays the resulting codec,
implementation, and hardware state using existing controller properties.

The strict CLI adds `--screen-encoder auto|software`. Interactive GUI calls
use `auto`. `software` is an audit-only mode that forces the existing
standard-bounded VP8 factory and reports `fallback:explicit-software`; it is not
shown as hardware and cannot satisfy quality/cinema gates. Baseline and
candidate measurements use the same executable SHA with different explicit
modes, eliminating binary drift while preserving a reproducible rollback.

### Deterministic moving-screen fixture

A small Qt Quick acceptance fixture renders color bars, high-contrast text,
thin lines, a moving block, and an increasing frame/time marker at the requested
profile cadence. It creates reliable Desktop Duplication changes and prevents
a static desktop from being misclassified as a cadence failure.

The fixture is test/acceptance tooling, not an application feature. It never
captures user content and is excluded from release packaging.

### Windows acceptance runner

`scripts/run_windows_screen_acceptance.py` orchestrates:

- the deterministic fixture;
- signaling server;
- host and viewer ShareMe processes;
- GUI/action probes where safe;
- continuous sanitized process and media counters;
- exact artifact finalization with SHA-256.

It supports an explicit `software-baseline` mode and a default `hardware`
mode. It may never label a fallback run as hardware. An interrupted or partial
run finalizes as failed with preserved partial counters.

## Quality and performance contract

The standard 1920x1080 at 60 fps profile is the controlled performance
comparison because both the existing VP8 fallback and Media Foundation H.264
can run at identical geometry and cadence.

Three sequential 180-second software-baseline runs and three sequential
180-second hardware-candidate runs are required. The measurement window is
30-150 seconds. Every host and viewer must have continuous CPU/RSS samples.

Hardware acceptance requires all of the following:

- actual host/viewer geometry is identical and does not exceed or silently
  reduce the requested profile;
- standard reaches 1920x1080 on a compatible display and submitted cadence is
  at least 95% of the 60 fps target during deterministic motion;
- quality reaches 2560x1440 at at least 95% of 60 fps on compatible hardware;
- cinema reaches 3840x2160 at at least 95% of 30 fps on compatible hardware;
- no unplanned application-layer drops or coalescing;
- `max_pending <= 1` throughout;
- nonzero bitrate, encode/decode, viewer presentation, and bidirectional voice
  counters with no more than five consecutive one-second stalls;
- one bounded presentation recovery and subsequent fresh-frame progress;
- H.264 is negotiated and `hardware_encoder_status=active` with
  `encoder_implementation=MediaFoundation` for all hardware runs;
- the standalone codec round trip passes Y-PSNR >= 35 dB, U/V-PSNR >= 32 dB,
  and Y-SSIM >= 0.95 with no missing matched frame;
- median standard host CPU mean improves by at least 30% against the
  same-geometry VP8 baseline;
- standard host CPU P95 does not regress and host RSS P95 grows by no more than
  15%;
- no dimensions, cadence, voice, recovery, or error gate fails.

The CPU threshold is a delivery gate, not permission to reduce quality. If
hardware H.264 works but misses the CPU threshold, preserve the evidence and
optimize another copy boundary in a later stage. Do not tune the threshold or
reduce the stream.

Physical temperature is recorded manually after equal ambient/warm-up
conditions, but is not inferred from CPU. A user-visible reduction in heat is a
human acceptance item and must include duration, power mode, and whether the
machine was charging.

## GUI and human acceptance

Windows GUI acceptance covers 100%, 125%, 150%, and 200% scaling where the
machine permits:

- home, create, join, settings, help, call, details, recovery, and return-home;
- no clipped primary action, overlapping controls, unreadable text, or hidden
  leave control at the minimum supported window size;
- native keyboard navigation and screen-reader-accessible names;
- microphone and speaker toggles affect the real primary voice path;
- host preview shows the captured screen and viewer shows the received screen;
- settings and recent-room privacy boundaries match macOS.

Two physical Windows devices then verify:

- room creation and join through the GUI;
- readable text, gradients, color bars, motion, and cursor behavior;
- sustained voice in both directions, speaker audibility, and acceptable echo;
- mute/unmute and speaker off/on behavior;
- minimize, restore, display sleep/wake where safe, and one bounded viewer
  presentation recovery;
- clean leave/rejoin and process shutdown;
- subjective heat/fan behavior during a 20-minute standard call and a
  10-minute quality or cinema call supported by the displays.

Human observations are stored as a checklist with machine classes and display
capabilities, not personal device names or room IDs.

## Failure and rollback policy

- The standalone Media Foundation probe is a hard checkpoint. If it cannot
  produce and then decode monotonic H.264 with exact geometry and reliable
  encoder/decoder release, product integration does not begin.
- VP8 remains the rollback and unsupported-hardware fallback.
- A fallback may verify call continuity but cannot satisfy the hardware gate.
- No mid-call codec swap is attempted.
- Partial artifacts are retained under ignored `out/`; generated fixtures,
  logs, screenshots, caches, and local settings are never committed.
- The repository-external libwebrtc cache remains read-only.
- Every checkpoint has a focused commit and an exact rollback SHA.

## Stage outcomes

The final handoff must use exactly one outcome:

- `verified-windows-gui-hardware-parity`: all automated, hardware, quality,
  performance, and two-device human gates pass;
- `partial-windows-hardware-evidence`: hardware path works but performance,
  display, acoustic, or two-device evidence is incomplete;
- `blocked-on-media-foundation-boundary`: standalone encoder feasibility or
  product integration fails, with VP8 baseline preserved.

## Explicit exclusions

- file sharing;
- Movie Stage 2B, drift correction, or hard resync;
- system-audio capture;
- remote input/control;
- TURN/public-network deployment;
- HDR tone-mapping claims;
- D3D11 zero-copy capture-to-encoder;
- 4K60;
- installer, code signing, auto-update, or store publication.

Packaging requirements may be inventoried after parity passes, but packaging
implementation belongs to the following stage.
