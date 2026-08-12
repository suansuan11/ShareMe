# macOS Screen Stability Verification

## Outcome

`verified-macos-screen-stability-evidence`

The long-run macOS screen smoke now owns a deterministic motion fixture,
guards its process for the full measurement, records only sanitized lifecycle
state, and makes the fixture window explicitly visible in the foreground. The
final 30-second and 120-second native runs passed without changing the product
ScreenCaptureKit, VideoToolbox, WebRTC, resolution, cadence, bitrate, queue, or
quality contracts.

This stage changes test orchestration and the test-only QML fixture. It does not
verify Windows Media Foundation and does not replace the pending hardened
Windows six-run comparison.

## Environment

- Date: 2026-08-12
- Platform: macOS 26.6.1 (25G76), arm64
- Base: `main` at `6667b3e`
- Branch: `codex/macos-screen-stability`
- Demo SHA-256:
  `c71e6b59acc5e9c53971eaf06a7a237996ba114a8389991d79895ae540f1fea7`
- Motion fixture SHA-256:
  `3fb364fbbc0d899b9b144b5dd52e076044a8e5682450905d63d337efe31a13ce`
- Locked external WebRTC root was consumed read-only.

The WebRTC source checkout remained detached and clean. `depot_tools` remained
on `main` and clean. No external archive, checkout, build, or cache was changed
or rebuilt.

## Root-cause evidence

### Stale binary diagnostic

The first controlled attempt used an old ignored demo binary from the main
checkout. It exited before room creation because it did not recognize the
current screen-encoder and audio CLI options. Rebuilding the current `main`
demo removed that failure. It is classified as a stale local build diagnostic,
not a product result.

### Static-desktop ambiguity

The earlier 30-second run without controlled motion advanced initially, then
stopped video counters while voice continued. A manually visible moving fixture
made the same current binary pass for 30 seconds, proving that an unchanged
desktop can trigger the existing strict continuity gate without proving a
VideoToolbox failure.

### Fixture visibility defect

The first runner-owned 120-second attempt still failed even though the fixture
process remained alive. The layered counters showed:

- host capture callback and submitted counters stopped together for up to 13
  samples and later resumed;
- host encoded stopped for up to 14 samples;
- viewer received/decoded/submitted stopped for up to 15 samples;
- voice counters continued;
- the fixture process remained alive through measurement and cleanup.

Artifact SHA-256:
`4cfda06c17df8070e64e514078b4edafcba4dcd67ce703e27e1aca89c0403c93`.

This placed the first failure before VideoToolbox: the live QML process did not
guarantee that its moving window was actually visible on the captured desktop.
The test-only fixture already used `WindowStaysOnTopHint`; adding `raise()` and
`requestActivate()` at QML root completion made the moving content visible and
removed the capture callback plateaus in the repeated 120-second run.

## Implemented evidence contract

`run_screen_stream_smoke.py` accepts `--motion-fixture PATH`. When supplied it:

1. rejects a missing fixture before creating an artifact;
2. starts the fixture before signaling with the selected profile and a bounded
   duration that includes startup/cleanup margin;
3. removes any inherited Qt offscreen setting;
4. uses the existing process group and Windows Job safeguards;
5. verifies survival during readiness and every measurement poll;
6. terminates the owned fixture on success, failure, or interruption;
7. writes only requested/started/alive/stopped booleans, never fixture paths,
   PIDs, room IDs, or child output.

Calls without `--motion-fixture` preserve their existing behavior and schema.
Windows acceptance continues to own its existing external fixture guard.

## Native acceptance

### Standard 30 seconds

Artifact SHA-256:
`7e7b0902ba3c6c9233bc88c61d49f312b39733e3a974f30c182e96119e0211a9`.

| Signal | Host | Viewer |
| --- | ---: | ---: |
| encoded / decoded | 1617 | 1606 |
| callback | 1657 | 1654 |
| submitted | 1657 | 1648 |
| voice packets sent | 1405 | 1412 |
| voice packets received | 1420 | 1397 |
| maximum continuity stall | 0 | 0 |

H.264 negotiated, VideoToolbox remained active, geometry matched at 1470x956,
bitrate remained nonzero, the viewer recorded one bounded presentation
recovery with 1549 post-recovery submissions, and fixture
started/alive/stopped were all true.

### Standard 120 seconds

Artifact SHA-256:
`7916fc1bcb7be3b14e28bffe9bca6858fbf34f23f4faa2ffacc8519854d3a0c1`.

| Signal | Host | Viewer |
| --- | ---: | ---: |
| encoded / decoded | 6810 | 6804 |
| callback | 6842 | 6840 |
| submitted | 6842 | 6830 |
| voice packets sent | 5920 | 5985 |
| voice packets received | 5989 | 5916 |
| observed continuity samples | 118 | 119 |
| maximum continuity stall | 0 | 1 |

H.264 negotiated, VideoToolbox remained active, geometry matched at 1470x956,
bitrate remained nonzero, queues remained bounded, the viewer recorded one
recovery with 6723 post-recovery submissions, and fixture
started/alive/stopped were all true.

## Automated regression

- Fresh `call-dev` configure/build: passed.
- Full `call-dev` CTest: 51/51 passed; final run took 18.86 seconds.
- `signaled_peer`: 20/20 consecutive runs passed.
- Affected Python suites under system Python 3.9.6: 36 passed with one
  configured Windows fixture test skipped.
- The same suites under Homebrew Python 3.14.6: 36 passed with the same one
  environment-dependent skip.
- `go test -race ./...`: passed.
- `go vet ./...`: passed.
- Sol-Terra workflow: 8/8 passed.
- ShareMe skill validator: passed.
- Portable-core forbidden platform/framework header scan: empty.
- `git diff --check`: passed.

## Evidence boundaries

- **Verified on macOS:** owned fixture lifecycle/redaction, foreground motion,
  native 30/120-second ScreenCaptureKit to VideoToolbox H.264 call, video and
  synthetic voice counter continuity, bounded presentation recovery, full
  call-dev regression, and repository gates.
- **Partial:** the test proves callback and RTC continuity under deterministic
  motion; it does not establish physical display scanout or subjective image
  quality.
- **Environment-dependent:** hardened Windows six-run performance comparison,
  two-device visual/acoustic checks, physical thermal observation, 4K,
  minimize/restore, display sleep/wake, cursor composition, and display
  selection.
- **Unimplemented or postponed:** file sharing, system audio, HDR, remote
  input, TURN, Movie Stage 2B, and 4K60 optimization.
