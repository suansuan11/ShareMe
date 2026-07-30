# Signaled Microphone Call Design

## Goal

Prove a real bidirectional microphone capture path over the existing
two-process Qt/Go/WebRTC call while retaining test video for connection and
rendering evidence.

## Scope

The call probe gains an explicit `--audio synthetic|microphone` option. Both
peers must select the same mode. Microphone mode creates the native audio device,
performs platform permission preflight, enables echo cancellation, noise
suppression, and automatic gain control, and never falls back to synthetic
audio when permission or device initialization fails.

Remote speaker playout remains disabled in this slice. That prevents feedback
when two test processes run on one Mac and keeps output-device and volume policy
out of the capture acceptance boundary. Test video remains 640x360 at 30 fps.

## Architecture

`SignaledPeerConfig` carries the audio mode into the peer controller. The
controller uses the existing `create_audio_device` and `audio_options`
contracts, so synthetic and microphone behavior share one lifecycle. Typed
creation failures expose stable categories to the probe without logging device
identifiers or native diagnostics containing private data.

The macOS call probe embeds an `NSMicrophoneUsageDescription`. The automation
script accepts an audio-mode argument and continues to require nonzero video,
audio send, and audio receive counters from both peers. Microphone acceptance
also requires nonzero local audio level from both processes.

## Acceptance

- Core tests verify configuration validation and processing-policy selection.
- Existing synthetic dual-process smoke remains green.
- Two microphone-mode processes connect through the local Go service and report
  video frames, bidirectional audio RTP, and nonzero local audio level.
- Permission denial or missing device exits nonzero with a typed category.
- Go race/vet, default CTest, and the complete Qt+WebRTC CTest suite pass.

This does not verify remote speaker playback, two physical computers, Windows,
TURN, movie audio, device selection UI, or long-duration echo performance.
