# Screen Streaming Quality and Voice Acceptance Design

## Purpose

Turn the accepted macOS hardware screen-streaming foundation into a real
screen-sharing call boundary. The application must use a microphone and play
the remote primary voice track in interactive mode, while deterministic smoke
tests retain synthetic audio and disabled playout to avoid acoustic feedback.

This stage also makes screen-profile geometry, video progress, voice RTP
progress, bounded presentation, and recovery evidence auditable in one
sanitized JSONL artifact.

## Frozen scope

Included:

- explicit `microphone` and `synthetic` primary-voice modes;
- explicit native primary-voice playout policy;
- microphone plus native playout as the interactive screen-call default;
- synthetic voice plus disabled playout in automated smoke runs;
- non-blocking periodic voice RTP statistics alongside existing video stats;
- profile-bound, aspect-ratio, video-progress, and voice-progress gates;
- a deterministic presentation recovery probe that exercises close/reopen of
  the application-owned preview ingress without rebuilding the call;
- macOS native runtime evidence and a manual visual/voice checklist.

Excluded:

- system or process audio capture;
- MovieAudioPeer, Movie Stage 2B, hard resync, or movie performance work;
- codec, bitrate, resolution, framerate, or degradation-policy changes;
- HDR, 4K60 optimization, remote input, TURN, public-network certification;
- Windows native capture/encoder implementation;
- automatic color-quality judgments that could reject legitimate solid-color
  desktop content.

File sharing remains postponed.

## Architecture

### Primary voice policy

`SignaledPeerConfig` owns two independent decisions:

1. `audio_mode`: `microphone` or `synthetic` recording source;
2. `native_audio_playout`: whether WebRTC may play the remote primary voice.

The interactive RTC demo defaults to `microphone` and native playout. The
screen smoke runner passes `--audio synthetic --no-audio-playout` explicitly.
Movie audio remains a separate peer and renderer and is not changed.

### Unified periodic media statistics

Extend the existing `SignaledVideoStats` polling result into
`SignaledMediaStats`. It contains the existing video fields plus voice packet
and byte totals for only the expected primary voice tracks. Track filtering
reuses `is_expected_voice_rtp_track` and
`is_expected_inbound_voice_rtp_track`, so Movie audio cannot be counted.

The existing background worker remains the only caller of `GetStats`. Qt timer
code reads a mutex-protected snapshot and never blocks on WebRTC.

### Acceptance runner

Each per-second counter line adds:

- `voice_packets_sent`;
- `voice_packets_received`;
- `voice_bytes_sent`;
- `voice_bytes_received`;
- `presentation_epoch`;
- `presentation_recovery_count`.

The runner validates:

- dimensions are positive, even, and within the selected profile;
- host and viewer dimensions agree and preserve their observed aspect ratio
  within one pixel of rounding;
- encoded/decoded/submitted counters make forward progress;
- primary voice sent/received counters are present, monotonic, and positive on
  both peers;
- no more than five consecutive one-second samples lack voice packet progress;
- the presentation queue remains bounded to one;
- no conversion failure or unavailable stats sample occurs after warm-up;
- a recovery probe increments the presentation epoch and submissions resume
  with no replay backlog or call rebuild.

The five-second voice window tolerates Opus DTX and scheduler jitter without
claiming physical sound quality. Manual microphone/speaker listening remains a
separate acceptance item.

### Presentation recovery probe

Under the smoke-only environment flag, the viewer schedules one recovery probe
after media is established. It closes the preview adapter ingress, immediately
reopens it with the existing sink, increments `presentation_epoch`, and records
the first post-recovery submission. It does not stop signaling, recreate a
PeerConnection, or alter audio.

This verifies the application latest-frame recovery contract. Actual macOS
minimize/restore behavior remains manual evidence because OS window automation
and permissions are not deterministic enough for a merge gate.

## Failure behavior

- Invalid audio CLI combinations fail before capture or signaling starts.
- Microphone permission or initialization failure is surfaced by the existing
  peer failure category; it never silently falls back to synthetic voice.
- Missing or regressing voice stats fail the acceptance runner as missing
  evidence, not as zero traffic.
- A recovery probe that fails to resume presentation fails the run while
  preserving sanitized partial JSONL.
- The call is never rebuilt solely to recover preview presentation.

## Evidence and platform boundaries

Automated macOS tests can verify microphone-mode selection, native playout
policy wiring, primary-track stats filtering, synthetic RTP continuity,
bounded presentation recovery, and native VideoToolbox screen media.

Human macOS acceptance is still required for actual microphone audibility,
speaker output, echo behavior, visible color integrity, foreground/background
window behavior, and physical temperature. Windows native behavior remains
environment-dependent until implemented and run on Windows.

## Completion gate

The stage is mergeable when focused tests, `call-dev`, `movie-call-dev`, Go
race/vet, workflow validation, and fresh standard/quality/cinema native smoke
pass; the final evidence clearly separates synthetic RTP proof from human
microphone/speaker proof; and no Critical or Important review finding remains.

