# macOS Native Capture Delegate Fault Gate Verification

## Result

Status: `verified-controlled-native-delegate-fault`

ShareMe now has a macOS-only, opt-in diagnostic gate that invokes the active
ScreenCaptureKit delegate's real `stream:didStopWithError:` entry. The normal
late-error monitor observes the sanitized `screen-capture-stopped-9001`
category and enters the existing bounded automatic recovery policy; the
trigger does not call the recovery policy directly.

Recovery performs and acknowledges a real `stopCapture` completion before it
starts the replacement stream. A non-nil native stop error fails closed: it
cannot emit the acknowledgement, cannot start the replacement, and follows the
bounded retry/exhaustion path. After successful replacement, a second
diagnostic invokes the retired delegate. Its captured old generation is
rejected without changing the replacement error or starting another recovery.

This verifies a controlled software invocation of the production Objective-C
delegate route. It does not claim that macOS physically produced a sleep/wake,
lock, permission, or display-removal error.

## Implementation boundary

- `ScreenCaptureBackend`, `ScreenVideoSource`, `MacScreenCaptureSource`, and
  `MacScreenCaptureStream` expose default-disabled diagnostic forwarding.
- Only the Apple implementation handles the commands; default and Windows
  adapters remain false/no-op.
- The current command invokes the active `ShareMeScreenCaptureDelegate`, whose
  existing block calls `handle_stream_error` with the active generation.
- Synthetic code 9001 remains distinguishable only inside the native adapter,
  so recovery still calls and waits for real `stopCapture` instead of assuming
  ScreenCaptureKit has already stopped.
- The old stream/delegate are retained only after a successful native stop and
  replacement startup. Stale injection, failed stop, failed startup, shutdown,
  and controller cleanup release the diagnostic state.
- The controller trigger path is compiled only on macOS and emits sanitized
  lifecycle markers. It never serializes paths, native identities, or NSError
  descriptions.
- Signaling, PeerConnection, video/voice tracks, VideoToolbox selection,
  dimensions, cadence, bitrate, queues, cursor, adaptation, audio routing, and
  retry delays were not changed.

## Native macOS acceptance

Environment:

- macOS 26.6.1 build 25G76, arm64;
- MacBook Air Mac16,12, Apple M4, 16 GB;
- native Cocoa, standard screen profile, H.264 auto selection;
- one host and one viewer with bidirectional synthetic primary voice;
- runner-owned moving fixture, 60 seconds;
- current delegate fault at 15 seconds with a three-second fixture suspension;
- retired delegate fault after two recovered counter samples.

Final ignored artifact:

- `out/macos-native-capture-fault-gate/native-delegate-60s-final.jsonl`
- JSONL SHA-256:
  `37dfe4cd2024a60b3dd7939d60f17fd4ccdd1e8833a4aac006bfc1e1f4de766b`
- demo SHA-256:
  `d5337097d24dcbda5888598a005648def48d6532ce450c79cf2fac616a38473c`
- motion fixture SHA-256:
  `9e9379b5bc50ab095b50dd2debde62e96154c55819ac48e0e76888accacebcec`

Observed boundaries:

- fixture suspended at host/viewer counter sample 13;
- recovery acknowledged before resume at sample 16;
- capture attempt/success/generation changed exactly `0/0/0 -> 1/1/1`;
- the transition was observed in two samples;
- real native old-stream stop acknowledgement preceded replacement success;
- host and viewer video each recovered in one sample;
- retired delegate injection was acknowledged at sample 18;
- restart counters remained exactly `1/1/1` after the stale callback;
- 40 post-stale samples remained;
- viewer presentation recovery was exactly one and had 3213 later
  submissions;
- H.264 negotiated and VideoToolbox remained active;
- host/viewer geometry matched at 1470x956;
- full-call host/viewer maximum continuity stall was zero samples;
- fixture was alive before cleanup and stopped during cleanup.

Final media counters:

| Signal | Host | Viewer |
| --- | ---: | ---: |
| encoded / decoded | 3293 | 3284 |
| callback | 3374 | 3321 |
| submitted | 3372 | 3314 |
| voice packets sent | 2913 | 2939 |
| voice packets received | 2945 | 2906 |
| voice bytes sent | 236057 | 235490 |
| voice bytes received | 235976 | 235490 |
| final bitrate (bps) | 2966392 | 2946232 |

## Regression evidence

- Fresh isolated baseline and final `call-dev`: 52/52 CTest passed.
- `signaled_peer`: 20 consecutive repetitions passed.
- RTC demo CLI contract: 48 tests passed with one configured skip under both
  Homebrew Python and system Python.
- Screen smoke contract: 25/25 passed under both Python interpreters.
- Go `test -race ./...` and `go vet ./...`: passed.
- Sol-Terra workflow: 8/8 passed.
- ShareMe skill validator: passed.
- Portable-core forbidden-header scan: empty.
- Final JSONL redaction scan and `git diff --check`: passed.
- Repository-external WebRTC checkout remained clean and was not modified.
- Independent Terra review found four Important lifecycle/evidence issues over
  two rounds: an active synthetic old stream, missing retained-object cleanup,
  false native-stop acknowledgement on NSError, and failed-start retention.
  All were fixed. The final review at `ff50698` found no Critical or Important
  issue.

## Evidence labels

- **Verified on macOS:** controlled invocation of the production Objective-C
  delegate error route; sanitized error propagation; real native old-stream
  stop acknowledgement; bounded same-source automatic recovery; old-generation
  delegate rejection; H.264 VideoToolbox; matching geometry; host/viewer video
  recovery; bidirectional synthetic voice continuity; presentation recovery;
  timeout, cleanup, and redaction behavior.
- **Partial:** the fixed synthetic NSError exercises the real delegate route but
  cannot represent every NSError value or operating-system transition.
- **Environment-dependent:** unsolicited physical ScreenCaptureKit failure,
  sleep/wake, lock, permission revocation, display removal, audible voice,
  physical scanout, subjective quality, physical thermals, Windows native
  rerun, and physical 4K displays.
- **Unimplemented:** system audio, HDR, remote input, TURN, file sharing, and
  4K60 optimization.

## Next Mac stage

The next Mac acceptance stage should be an authorized physical lifecycle
campaign: run sleep/wake and lock/unlock first, then display removal only when
external-display hardware is available. Preserve the current media-quality and
voice gates. Do not describe permission revocation or display removal as
verified unless the exact physical event is performed and recorded.
