# Screen Streaming Quality and Voice Acceptance

## Scope and quality contract

This stage keeps the accepted ScreenCaptureKit and VideoToolbox screen path and
adds the missing primary-call acceptance boundary:

- the interactive RTC demo defaults to the real microphone and native remote
  voice playout;
- automated screen smoke calls explicitly use synthetic voice and disable
  speaker playout, so they are deterministic and cannot create acoustic echo;
- primary voice RTP packet and byte totals are collected with the existing
  asynchronous media-stats poller;
- the runner requires host/viewer geometry equality, monotonic video and voice
  progress, no more than five consecutive no-progress samples, and no stats
  loss after warm-up; and
- a screen-only probe closes and reopens the bounded Qt presentation ingress
  exactly once without rebuilding the call or replaying a pending stale frame.

No resolution, frame-rate, bitrate, drop policy, or profile threshold was
reduced. System audio, HDR, remote input, TURN, file sharing, Movie Stage 2B,
hard resync, and Windows-native implementation are outside this stage.

## Implementation boundary

The code rollback point is `faaea73`; the reviewed implementation tip before
this evidence update is `0908b53` on `codex/screen-voice-acceptance`.

- `SignaledPeerConfig::native_audio_playout` controls WebRTC native playout and
  the received primary audio track is enabled under the same policy.
- `SignaledMediaStats` reports video totals plus primary-voice packet and byte
  totals. Track filtering excludes independent movie audio.
- The Qt timer reads a mutex-protected snapshot produced by one background
  poller; it does not call `GetStats` or wait on WebRTC directly.
- `VideoPreviewAdapter::close_ingress()` clears the pending frame and detaches
  the sink. `reopen_ingress()` increments the presentation epoch and accepts
  only subsequent frames. Pending depth remains bounded to one.
- `SHAREME_SCREEN_RECOVERY_PROBE` is honored only for the screen viewer. It is
  not a general movie or desktop lifecycle switch.

## Automated verification

Verified on macOS 26.6.1 (25G76), Apple M4, arm64:

- `call-dev` configured against the preserved external WebRTC package, built,
  and passed CTest 39/39;
- `movie-call-dev` configured against the same package, built, and passed CTest
  64/64;
- focused `signaled_peer`, RTC demo CLI, screen-smoke contract, and video
  preview adapter tests passed;
- the real peer test observed nonzero sent/received primary-voice packets and
  bytes on both roles;
- Go `test -count=1 -race ./...` and `go vet ./...` passed;
- the Sol-Terra workflow suite passed 8/8, the ShareMe skill validator passed,
  the portable-core forbidden-header scan was empty, and `git diff --check`
  passed after documentation cleanup.

The existing Qt 6.8 deprecation warning for the internal `QVideoFrame` buffer
constructor remains a Minor compatibility item. It was present before this
stage and did not fail compilation or runtime gates.

## Native screen, video, voice, and recovery gates

All five runs used distinct local ports and sanitized ignored JSONL artifacts.
Every run reported H.264, `hardware_encoder_status=active`, identical
1470x956 host/viewer geometry, one presentation recovery, post-recovery frame
progress, nonzero bitrate, and bidirectional primary-voice RTP progress.

| Profile and duration | Host encoded | Viewer decoded | Host voice sent/received | Viewer voice sent/received | Maximum stall | Post-recovery submissions | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| standard 10 s | 517 | 510 | 452 / 455 | 450 / 447 | 0 | 400 | `98e62a9a4fd4a011251e23398ce2c636f92e14ffee231791ac16b7f68f872578` |
| standard 30 s | 1673 | 1668 | 1455 / 1470 | 1466 / 1451 | 0 | 1557 | `03f79583f34592ec3489ba8e6e59b85372a57a46d33cc329e0285cb87260efe9` |
| quality 30 s | 1613 | 1606 | 1405 / 1420 | 1415 / 1400 | 1 | 1544 | `44992abf5ef551a6bb097f5dbb8e3fa4a70c4f8f69d653dafa105ad95bfe4204` |
| cinema 30 s | 852 | 849 | 1455 / 1470 | 1465 / 1451 | 0 | 758 | `d393008748cb975a6350d7cfe3884fabde5751f5d5c7abdca140fae3bc42b22a` |
| cinema 120 s | 3471 | 3498 | 5922 / 5991 | 6037 / 5968 | 1 | 3397 | `1ef837765bcc7432ede66ec024653dabd875751d614c49bcc6ff7a5a40ad2970` |

The current physical display supplies 1470x956 frames. These runs therefore
verify exact agreement at the actually captured geometry and retention of each
profile policy, but they do not prove physical 1920x1080, 2560x1440, or
3840x2160 capture.

## Microphone and acoustic evidence

- **Verified — microphone capture and RTP:** a native microphone probe reported
  nonzero local levels on both roles (`0.00506607` host and `0.0125431` viewer)
  and bidirectional audio RTP. The viewer completed with 101 sent and 101
  received packets. The host reported 745 sent and 101 received packets.
- **Partial — legacy probe result:** that probe returned nonzero because it
  still requires the screen host to receive remote video. The accepted screen
  architecture is intentionally host-video-send-only and
  viewer-video-receive-only, so the failure category was `no remote video
  received`; it was not an audio failure. The product direction was not
  regressed to satisfy this stale bidirectional-video assumption.
- **Verified — runtime policy wiring:** RTC demo validation accepts explicit
  microphone mode, and interactive defaults pass microphone plus native
  playout into the primary PeerConnection.
- **Environment-dependent — human acoustic acceptance:** no claim is made that
  a person heard the remote speaker output, that echo cancellation was
  subjectively acceptable, or that audio remained perceptually continuous
  across a physical foreground/background action. Those require a two-device
  listening test.

## Review and evidence boundaries

- **Verified:** macOS build/test health, native ScreenCaptureKit delivery,
  VideoToolbox H.264 activation, actual-capture geometry agreement, synthetic
  bidirectional primary-voice RTP continuity, one bounded presentation
  close/reopen, stale-frame clearing, and continued post-recovery delivery.
- **Partial:** microphone capture/RTP is verified separately from the screen
  smoke; native speaker wiring is code- and configuration-verified but not
  human-listened; visual color/text integrity is not scored by an image metric.
- **Environment-dependent:** a physical 1080p/1440p/4K display, two-device
  speech/echo testing, actual window foreground/background behavior, display
  scanout, physical temperature, and all Windows-native media evidence.
- **Unimplemented:** Windows GPU screen parity for this stage, system audio,
  HDR, remote input, TURN/public-network acceptance, and automatic hard resync.

The full branch review found no Critical or Important lifecycle, stats-thread,
audio-isolation, privacy, or scope issue after restricting the recovery probe
to screen mode. The repository-external WebRTC checkout remained clean and was
not modified or cleaned. Generated build trees and JSONL evidence remain
ignored and are not commit candidates.
