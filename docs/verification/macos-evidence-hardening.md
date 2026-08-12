# macOS Evidence Hardening Verification

## Outcome

`verified-macos-evidence-hardening`

The portable performance-evidence tooling now rejects discontinuous process
samples, reused artifact paths, reused or malformed run identities, and missing
or malformed executable SHA-256 values. New screen-smoke artifacts carry a
random, non-sensitive `run_id`. The shared macOS application boundary also
passed its required 10-second native VideoToolbox and GUI lifecycle regressions.

This result does not rerun or verify Windows Media Foundation. The Windows
three-plus-three performance comparison recorded before this hardening remains
historical evidence; a new formal comparison must be collected on Windows with
six independent artifacts produced by the hardened runner.

## Environment

- Date: 2026-08-12
- Platform: macOS 26.6.1 (25G76), arm64
- Branch: `codex/macos-evidence-hardening`
- Base: `main` at `d15c139`
- Locked WebRTC root: `/Users/dio/Library/Caches/ShareMe/webrtc`
- Demo SHA-256:
  `b1d57f4232ca35caae282ffc00c2a543db44c971f8a9da0b66acfd204d6da1e4`

The external WebRTC source checkout remained detached and clean. The external
`depot_tools` checkout remained on `main` and clean. Neither checkout, its
archives, nor the cache was modified or rebuilt.

## Evidence contract changes

### Continuous process window

`measurement_window` now requires exactly one sample for every integer second
from 30 through 150 inclusive. Elapsed values must be integers; CPU values must
be finite and nonnegative; RSS values must be positive integers. Missing,
duplicated, unordered, malformed, or invalid samples fail instead of producing
a comparison.

### Independent run and binary identity

Every new `kind=run` screen-smoke record includes a UUID-derived lowercase
32-hex `run_id`. A formal Windows standard comparison now requires:

- six distinct resolved artifact paths;
- six valid and distinct run identities;
- strict lowercase 64-hex executable SHA-256 values;
- one common executable SHA-256 across all six runs, so the comparison cannot
  silently mix builds while run independence remains separately proven.

Run identity is intentionally independent of the machine, user, path, room,
time, or process ID. Existing JSONL schema fields are retained.

## Automated verification

### Script contracts

The five affected script suites passed under both the macOS system Python
3.9.6 and Homebrew Python 3.14.6:

- 31 tests passed;
- 1 fixture-dependent test skipped because no Windows artifact fixture was
  configured;
- the GUI acceptance module now opts into postponed annotation evaluation so
  its existing type hints remain importable by the default macOS Python.

The implementation was developed with explicit RED then GREEN tests for a
missing measurement second, missing run identity, missing comparison helper,
missing summary identity, and duplicated run identity.

### Native builds and CTest

- Fresh `call-dev` configure/build succeeded against the preserved WebRTC root.
- `call-dev`: 51/51 CTest tests passed in 36.50 seconds.
- `signaled_peer`: 20/20 consecutive lifecycles passed.
- Fresh `movie-call-dev` configure/build succeeded with Qt and FFmpeg.
- `movie-call-dev`: 76/76 CTest tests passed in 57.42 seconds.

### Native screen and GUI smoke

The required standard 10-second screen run completed successfully. It
negotiated H.264, reported active VideoToolbox hardware encoding, and maintained
matching 1470x956 geometry:

| Signal | Host | Viewer |
| --- | ---: | ---: |
| encoded / decoded | 456 | 501 |
| callback | 513 | 506 |
| submitted | 513 | 501 |
| voice packets sent | 400 | 445 |
| voice packets received | 403 | 442 |
| maximum continuity stall | 0 | 0 |

The viewer recorded exactly one bounded presentation recovery and 392
post-recovery submissions. The ignored JSONL artifact SHA-256 is
`8bbdf67a2048a2beede02ce0ceabc0fe5a14b659d3d21e9b133543876a2c323f`.

The GUI lifecycle smoke passed all 6 probes with 39 idle samples. Its ignored
artifact SHA-256 is
`6fb2bb4b5c0ab09329ac1bab12b65bca629358601ec42256408a02595f0b25ce`.

### Additional diagnostic retained

A separate 30-second standard run connected, negotiated H.264, and initially
advanced video and voice counters, but video counters stopped advancing late
in the run and the continuity gate rejected it as `host encoded stalled for
too long`. Its incomplete artifact SHA-256 is
`e098fa248f436010ec4ec988b7f3794373e4e2fdfca969c8a6993c110c84f0c5`.

The exact cause was not established during this bounded tooling stage. It is
retained as a negative diagnostic and is not presented as a successful
stability run. The frozen shared regression requirement for this stage was the
standard 10-second native smoke, which passed without lowering any threshold.

### Repository gates

- `go test -race ./...`: passed.
- `go vet ./...`: passed.
- Sol-Terra workflow: 8/8 passed.
- ShareMe skill validator: passed.
- Portable-core forbidden platform/framework header scan: empty.
- `git diff --check`: passed.

## Boundaries

- **Verified on macOS:** evidence validation contracts, fresh builds, full
  CTest suites, repeated RTC lifecycle, standard 10-second native H.264 screen
  path, VideoToolbox selection, video/voice counter progress, bounded viewer
  recovery, and GUI lifecycle.
- **Partial on macOS:** the separate 30-second diagnostic stalled after initial
  progress; human visual quality, human audio quality, physical temperature,
  and scanout were not measured.
- **Environment-dependent:** a new six-artifact Windows performance comparison,
  two-device Windows visual/acoustic acceptance, physical 4K, cursor
  composition, and display selection.
- **Unimplemented or postponed:** file sharing, system-audio capture, HDR,
  remote input, TURN, Movie Stage 2B, and 4K60 optimization.

## Next action

Run the formal Windows software-versus-Media-Foundation comparison again with
three independent baseline artifacts and three independent candidate artifacts.
Only a comparison that passes the hardened identity and continuous-sampling
contracts may replace the historical performance result.
