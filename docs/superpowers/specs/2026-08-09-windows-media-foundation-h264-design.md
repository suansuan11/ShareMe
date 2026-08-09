# Windows Media Foundation H.264 Design

## Goal

Deliver native Windows H.264 hardware encoding for ShareMe screen streams while
preserving the locked libwebrtc revision and the existing macOS VideoToolbox and
Windows VP8 fallback behavior.

## Scope

This stage owns the Windows WebRTC video codec boundary only:

- enumerate a Windows Media Foundation hardware H.264 encoder MFT;
- adapt WebRTC I420 frames to NV12 input and return Annex-B H.264 access units;
- decode received H.264 through a Media Foundation decoder so two Windows
  ShareMe clients interoperate;
- negotiate the existing packetization-mode 1 H.264 formats and profile levels;
- preserve primary voice, screen presentation recovery, signaling, and bitrate
  control behavior; and
- fail closed to the existing VP8 1920x1080 software path.

DXGI texture-to-MFT zero-copy, cursor composition, display selection, system
audio, TURN, and unrelated movie work are outside this stage.

## Architecture

The generic selection layer will use platform-neutral H.264 probe and factory
names. macOS will continue to implement those entry points with VideoToolbox.
Windows will implement them with Media Foundation. Other platforms return an
unavailable result and use VP8.

The Windows implementation contains three bounded units:

1. `WindowsMfRuntime` balances `MFStartup` and `MFShutdown` and exposes MFT
   enumeration helpers.
2. `WindowsMfH264Encoder` implements `webrtc::VideoEncoder`. It accepts I420,
   converts into one reusable NV12 input buffer, feeds a hardware encoder MFT,
   drains all immediately available output, converts length-prefixed NAL units
   to Annex-B when necessary, and invokes `EncodedImageCallback` without an
   unbounded queue.
3. `WindowsMfH264Decoder` implements `webrtc::VideoDecoder`. It accepts Annex-B
   H.264, feeds the Microsoft H.264 decoder MFT, converts decoded NV12 to I420,
   and returns a normal WebRTC `VideoFrame`.

The encoder enumerator uses `MFT_ENUM_FLAG_HARDWARE` and rejects transforms that
cannot be activated and configured for the requested dimensions/framerate. A
successful selection therefore means an actual hardware MFT was initialized,
not merely that H.264 exists on the machine.

## Codec Contract

- Codec: H.264 constrained baseline or baseline.
- Packetization: non-interleaved, SDP `packetization-mode=1`.
- Input: even-sized WebRTC I420 frames converted to NV12.
- Output: Annex-B H.264 access units with keyframe metadata.
- Levels: 4.2 for `standard`; 5.1 for `quality` and `cinema`.
- Rate control: WebRTC `SetRates` updates average bitrate and framerate on the
  active MFT when supported; unsupported live updates trigger a bounded encoder
  restart rather than an accumulating side queue.
- Frame pressure: at most one frame is synchronously submitted per `Encode`
  call; `MF_E_NOTACCEPTING` causes output drain and one retry. Persistent
  pressure returns a WebRTC encoder error.

## Selection and Diagnostics

`select_screen_video_encoder` continues to perform probe, factory creation,
format inspection, and `InitEncode` before reporting hardware active.

On Windows success it reports:

- `requested_codec=H264`;
- `negotiated_codec=H264`;
- `encoder_implementation=MediaFoundation`;
- `hardware_encoder_status=active`.

If probe, activation, configuration, or initialization fails, the selection
returns the existing VP8 factory, clamps capture to the standard profile, and
records a sanitized stable reason such as `mf-h264-hardware-unavailable`,
`mf-h264-activation-failed`, or `mf-h264-initialization-failed`.

## Error and Lifecycle Handling

- Media Foundation startup is process-shared and reference counted.
- COM objects remain owned by `Microsoft::WRL::ComPtr`.
- Encoder/decoder `Release` flushes the transform and is idempotent.
- Callback pointers are cleared before transform destruction.
- No Media Foundation object escapes its owning codec instance.
- Hardware errors are reported to WebRTC; selection-time failures use VP8.
- Runtime encoder failures do not silently claim hardware success.

## Testing

TDD coverage will include:

- generic selection reports platform implementation names and stable fallback;
- Windows hardware enumeration rejects software-only MFTs;
- I420-to-NV12 conversion, Annex-B normalization, keyframe detection, timestamp
  propagation, and invalid-buffer handling;
- encoder and decoder lifecycle and rate-control contracts through injectable
  transform boundaries;
- CMake isolation proving Windows libraries and headers do not enter macOS;
- full Windows `call-dev` and `movie-call-dev` builds and CTest suites; and
- native two-peer screen/voice smoke requiring H.264, Media Foundation active,
  nonzero encode/decode/bitrate, voice continuity, and one presentation recovery.

The native smoke is the acceptance authority for actual hardware activation.
Static inspection or a skipped hardware test is not sufficient.

## Acceptance Boundary

The stage is verified only if the current Windows host completes at least a
10-second `standard` native smoke with active Media Foundation H.264. `quality`
and `cinema` are reported separately as verified, partial, or
environment-dependent based on real runs. CPU reduction and zero-copy are not
claimed without separate measurements.
