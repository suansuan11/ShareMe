# Quality-Preserving Movie Playback Performance

Status: `blocked-on-quality-preserving-boundary`

Scope is macOS only for this handoff. Windows execution is environment-dependent
and was not performed. The study media is referred to only as `<MOVIE_PATH>` in
committed material.

## Delivered measurement and playback path

The branch now provides:

- a Qt-free quality contract with exact geometry, rational cadence and pixel
  aspect checks, color/codec metadata checks, drop checks, and finite PSNR/SSIM
  thresholds;
- a sequential three-run performance runner with explicit output containment,
  no-overwrite behavior, sanitized allowlisted counter parsing, concurrent
  host/viewer output monitoring, process-exit detection, CPU/RSS sampling, and
  preserved failure categories;
- direct FFmpeg-to-I420 movie frames with owned planes and strides, with a
  tested observable RGBA fallback;
- a bounded one-frame Qt planar-YUV preview adapter with an observable ARGB
  fallback and sink-submission counters; and
- explicit `--video-acceleration auto|software` validation. No hardware
  adapter was added because the captured evidence did not attribute the
  remaining cost to a codec boundary strongly enough to justify one.

No resolution, cadence target, bitrate, codec quality configuration, audio
path, MovieTimeline behavior, queue capacity, drop policy, or SyncController
threshold was changed. Qt sink submission is application-layer evidence only;
it is not evidence of display scanout, acoustic A/V synchronization, or real
screen presentation.

## Reproduction boundary

Build and run commands used the following sanitized form:

```text
cmake --fresh --preset movie-call-dev -DWEBRTC_ROOT=<WEBRTC_CACHE>
cmake --build --preset build-movie-call-dev --parallel 4
python3 scripts/run_movie_performance_study.py \
  --output-root <IGNORED_OUTPUT_ROOT> \
  --output-parent <IGNORED_OUTPUT_PARENT> \
  --run-count 3 \
  --demo <RTC_DEMO> \
  --server-url ws://127.0.0.1:<PORT>/v1/ws \
  --server-root <SERVER_ROOT> \
  --movie <MOVIE_PATH> \
  --video-acceleration software \
  --duration-seconds 180
```

The same command was run with `--video-acceleration auto` for the candidate.
Each run was one local signaling server, one movie host with independent movie
audio, and one viewer. The raw JSONL remained in ignored build-output
directories and is not part of Git.

## Three-run software baseline

The final frozen baseline is the phase-correct post-cleanup software path used
for the candidate comparison. Each run completed 180 seconds on Darwin with no
runner failure, decode failure, audio failure, or RTC failure recorded.

| Run | Host average CPU | Viewer average CPU | Host CPU P95 | Viewer CPU P95 | Host RSS P95 | Viewer RSS P95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 01 | 121.67% | 15.18% | 159.6% | 21.1% | 815,005,696 | 257,753,088 |
| 02 | 132.77% | 15.38% | 212.8% | 23.1% | 820,772,864 | 263,389,184 |
| 03 | 167.20% | 16.43% | 359.4% | 29.2% | 986,185,728 | 392,560,640 |

Observed host media metadata was 3840×2160, 24000/1001, 1/1 pixel aspect,
limited range, HEVC Main10. The source color-space field was `unknown` on this
FFmpeg build and was identical in baseline and candidate. The software
baseline SHA-256 values are:

```text
run-01.jsonl  ac95d188c0e6612d4e8beccdca469075eda8cb8a593c562c8c8e297859c1c6c0
run-02.jsonl  d40fb02d237074da57f050d7deb8128dcf4f375b0a3c4665e043ffdfe8cc7b42
run-03.jsonl  50427da1bf5bb213670fa2aa779130cc5841a12bb66c82297e4d37f29648c7ac
```

The earlier pre-cleanup forced-software diagnostic was also completed three
times and established the redundant conversion/copy hot-path hypothesis. It
is retained as ignored evidence only and is not used as the frozen gate
baseline because its pre-normalization pixel-aspect counter was invalid.

## Three-run auto candidate and gates

All three auto runs completed 180 seconds. The current `auto` mode has no
platform hardware adapter and therefore remains the unchanged quality-
preserving software path. Candidate SHA-256 values are:

```text
run-01.jsonl  54f1f4d6257ed5dae19bb5762c377dbc9275db2c38f550abafa69cd308233c2f
run-02.jsonl  c07fc4de30db46aef99770eb4df05ffb1118c9d68191556478d009415e39706c
run-03.jsonl  696f9eace4c84557f1a5a175a01d3aa2b1d0cdf469fef879afb6ebb24a4fef9a
```

| Gate | Result | Evidence |
| --- | --- | --- |
| Complete 180-second lifecycle | verified | 3/3 artifacts complete; failure is null |
| Exact dimensions and metadata | verified | 3840×2160, cadence, aspect, range, codec, profile match |
| Cadence ≥99% of baseline | verified | counter-based submitted ratio 1.0493; timestamp cadence was not independently sampled |
| No additional drops/coalescing | verified | aggregate additional count 0; no host coalescing was observed in either phase-correct set |
| PSNR ≥45 dB | failed / missing | no sampled decoded-frame pair was recorded |
| SSIM ≥0.995 | failed / missing | no sampled decoded-frame pair was recorded |
| Combined average CPU reduction ≥30% | failed | aggregate reduction 7.47% versus phase-correct software baseline |
| CPU P95 non-regression | verified | combined candidate P95 change -61.3 percentage points |
| RSS P95 growth ≤10% | verified | aggregate growth -3.381% |
| Paused CPU reduction ≥70% | failed / missing | dedicated paused probe not executed |
| One-frame GUI backlog bound | partial | adapter unit test passes; real run counter shows no candidate coalescing, but no display scanout proof |
| Preview/audio/pause/seek human checks | environment-dependent | no human GUI/audio confirmation was captured in this headless run |

The frozen candidate gate therefore did not pass. No threshold was changed and
no quality-degrading workaround was introduced. The remaining boundary is the
missing quality evidence (sampled PSNR/SSIM and human audio/pause/seek
confirmation), timestamp-level cadence evidence, and an evidenced codec/encode
boundary if a future hardware adapter is considered. The current data does not
justify claiming a 30% CPU reduction or claiming reduced physical temperature.

## Verification boundary

Verified on macOS: focused C++/Python contracts, direct-I420 media tests,
preview-adapter test, playback demo build, 3×180-second software baseline, and
3×180-second auto candidate lifecycle. Windows is environment-dependent and
unverified. Full-suite, lifecycle-repeat, Go, workflow, validator, final
read-only review, and human GUI/audio acceptance are recorded separately at the
stage handoff and are not inferred from this measurement document. Qt sink
submission remains application-layer evidence only; it does not verify display
scanout, acoustic A/V synchronization, or physical screen presentation.

## Stage verification

The completed macOS verification checkpoint passed full CTest 49/49,
including the standard-C++ portability regression contract, 20/20 repeated
`signaled_peer` lifecycles, `go test -race ./...`, `go vet
./...`, ShareMe Sol–Terra workflow tests 8/8, the ShareMe skill validator, and
`git diff --check`. These checks establish regression and tooling health; they
do not turn the failed performance gate into a pass. No Windows build, native
Windows media run, human preview/audio confirmation, display scanout, acoustic
A/V, or physical thermal measurement was performed.
