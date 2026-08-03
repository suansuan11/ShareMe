# Receiver Movie Audio Playout Design

Date: 2026-08-03 (Asia/Shanghai)

## Problem and evidence

The dedicated movie-audio PeerConnection successfully decodes and transports
48 kHz stereo PCM, but its runtime always uses a synthetic ADM with a discard
renderer and unconditionally calls `SetAudioPlayout(false)`. The receive sink
therefore measures PCM without ever opening a speaker device.

The supplied `01.mkv` contains HEVC Main10 video and FLAC 48 kHz stereo audio.
FFmpeg decodes both streams, and the existing call probe receives 100 valid
stereo audio callbacks with a nonzero peak. Its smoke run is currently rejected
for an independent test defect: the validator requires exactly 320x180 while
WebRTC adapts this 4K source to 960x540.

## Selected architecture

Add a playout-only native audio-device mode to the WebRTC audio-device factory.
It initializes the platform-default output and no recording device. A
`MovieAudioPeerConfig::native_playout` flag is valid only for viewers. When
enabled, the peer creates its private runtime with that native ADM and enables
PeerConnection audio playout; hosts and automated transport probes retain the
synthetic discard ADM and disabled playout.

The Qt RTC demo enables native movie playout for its viewer. The command-line
signaled-call probe keeps it disabled so deterministic smoke tests never depend
on CI audio hardware or emit sound. Voice and movie audio remain separate; no
mixing, microphone fallback, Qt PCM queue, or primary-Peer change is introduced.

## Device and failure behavior

- Native playout selects the default output, initializes speaker and stereo
  playout, and reports typed dependency/initialization failures.
- It never requests microphone permission and never initializes recording.
- Viewer peer creation fails with a sanitized movie-audio playout category when
  the selected output cannot initialize; it does not silently use a discard
  renderer.
- Stop remains idempotent and lets the private runtime stop playout before ADM
  termination.

## Test repair and verification

- TDD covers playout-only ADM initialization, no recording callbacks, typed
  failures, viewer-only config, and enabled/disabled PeerConnection policy.
- Movie smoke accepts adaptive 16:9 dimensions instead of one fixture-specific
  size while retaining the minimum-frame requirement.
- Existing generated-fixture smoke remains green.
- The supplied `01.mkv` must pass real decode/transport smoke with valid 48 kHz
  stereo PCM and nonzero peak.
- The Qt demo must build and its contract must require native viewer playout.
- Full CTest, Go race/vet, workflow gates, and `git diff --check` remain green.
- Audible speaker output is a manual macOS acceptance boundary; Windows native
  playout remains environment-dependent until rerun there.

## Git review result

`codex/movie-audio-isolation` is fully merged: it is an ancestor of `main`, has
zero unique commits, and is five commits behind the stage base. Both existing
worktrees were clean when this stage began. The older handoff sentence claiming
that isolation was not merged is stale and will be corrected.
