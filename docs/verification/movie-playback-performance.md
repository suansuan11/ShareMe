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
- explicit `--video-acceleration auto|software` validation, with a profiling-
  gated macOS HEVC VideoToolbox decode path and software fallback. The
  hardware candidate is recorded below but is not a verified deliverable
  because its quality gate failed. No Windows hardware adapter was executed.

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

The baseline and candidate used separate executable paths and SHA-256
identities. The baseline command used `<BASELINE_DEMO>` with
`--video-acceleration software`; the candidate used `<CANDIDATE_DEMO>` with
`--video-acceleration auto`. Each side ran three sequential times. Each run
was one local signaling server, one movie host with independent movie audio,
and one viewer. The raw JSONL remained in ignored build-output directories and
is not part of Git.

## Three-run software baseline

The review-corrected baseline is an independently built instrumented binary
from the pre-cleanup movie playback path at source `cd3045e` plus measurement-only
counter log sanitation; its executable
SHA-256 is
`7169750fffc0440c6f4ebfd87034356179290dd4a51b44e6aeda425fd153f4d2`.
All three runs completed 180 seconds on Darwin with 120 process samples per
role in the 30–150 second measurement window. CPU and RSS below exclude
warmup and finalization.

| Run | Host average CPU | Viewer average CPU | Host CPU P95 | Viewer CPU P95 | Host RSS P95 | Viewer RSS P95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 01 | 226.96% | 23.83% | 338.1% | 32.7% | 1,022,148,608 | 379,322,368 |
| 02 | 152.24% | 19.41% | 189.2% | 25.1% | 934,264,832 | 320,405,504 |
| 03 | 173.39% | 20.32% | 283.0% | 29.3% | 1,038,811,136 | 430,456,832 |

Observed host media metadata was 3840×2160, 24000/1001, limited range, HEVC
Main10. The source color-space field was `unknown` on this FFmpeg build. The
baseline pixel-aspect counter was 0/1 while the candidate counter was 1/1,
which is one of the failed exact-metadata checks. The baseline artifact
SHA-256 values are:

```text
run-01.jsonl  bab4a0e891e3789638a75acadbd1e1e38c8eb3b43637edf8b6c04c4396021d88
run-02.jsonl  c24c7bbf62a04ad8b061258f67af126aedb501c025724d5fb2897b150560f0ce
run-03.jsonl  27365752cb96f19884a6bdc46f5b297b58395372bda49ed6ed2cd8617d39fa6e
```

The earlier same-binary software/auto comparison is invalidated and is not
used as evidence. This baseline is the first comparison with distinct
executable identities and measurement-window-only process statistics.

## Historical three-run software `auto` candidate and gates

All three historical auto runs completed 180 seconds with 120 process samples
per role in the formal measurement window. This was captured before the
profiling-gated macOS hardware candidate below; that executable used the
unchanged software path. Its executable SHA-256 is
`0ef2bf6aebe8bee6ebdbc835e67eed94aa880a074ba86c560a4370712d6e18b9`.
The candidate measurement-chain fixes are recorded in local commit `4eecb83`.
Candidate artifact SHA-256 values are:

```text
run-01.jsonl  ba6c9fc4ad625f1192fa0d332b606ee729f838a905d2a2363623c744ceb61547
run-02.jsonl  752837f5be51603a892b29d1424c60d908fa144a85b2d20240c1cdb9433bd4ea
run-03.jsonl  8b90260803835ac418d296d3ecacdb02d22f1f0b90a6663e649ff317fc46fa59
```

| Run | Host average CPU | Viewer average CPU | Host CPU P95 | Viewer CPU P95 | Host RSS P95 | Viewer RSS P95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 01 | 129.20% | 17.68% | 170.3% | 23.0% | 730,152,960 | 256,655,360 |
| 02 | 177.93% | 21.88% | 355.0% | 32.9% | 1,048,395,776 | 364,347,392 |
| 03 | 127.13% | 16.86% | 150.3% | 21.1% | 732,856,320 | 250,445,824 |

| Gate | Result | Evidence |
| --- | --- | --- |
| Complete 180-second lifecycle | verified | 3/3 artifacts complete; failure is null |
| Exact dimensions and metadata | failed | per-run/per-role comparison failed: candidate viewer was 2560×1440 versus baseline 1920×1080 in run 02, and 1920×1080 versus baseline 3840×2160 in run 03; host pixel aspect was 1/1 versus baseline 0/1 |
| Cadence ≥99% of baseline | failed | minimum per-run/per-role submitted ratio was 85.23% in run 02 viewer; timestamp cadence was not independently sampled |
| No additional drops/coalescing | failed | run 02 recorded 22 candidate coalesced frames on both host and viewer |
| PSNR ≥45 dB | failed / missing | no sampled decoded-frame pair was recorded |
| SSIM ≥0.995 | failed / missing | no sampled decoded-frame pair was recorded |
| Combined average CPU reduction ≥30% | failed | median-of-three reduction 24.17% versus the independent baseline |
| CPU P95 non-regression | verified | combined candidate P95 change -119.0 percentage points |
| RSS P95 growth ≤10% | verified | median-of-three growth -29.59% |
| Paused CPU reduction ≥70% | failed / missing | dedicated paused probe not executed |
| One-frame GUI backlog bound | verified / application-layer | all six role/run summaries reported `max_pending=1`; no display scanout proof |
| Preview/audio/pause/seek human checks | environment-dependent | no human GUI/audio confirmation was captured in this headless run |

The frozen candidate gate therefore did not pass. No threshold was changed and
no quality-degrading workaround was introduced. The review findings are now
covered by distinct baseline/candidate identities, measurement-window-only
CPU/RSS, explicit sampling failures, and per-run/per-role comparisons. The
remaining boundary is the failed geometry/cadence/coalescing evidence plus
missing PSNR/SSIM, paused-probe, and human audio/pause/seek confirmation. The
current data does not justify claiming a 30% CPU reduction or claiming reduced
physical temperature.

## Profiling-gated macOS hardware candidate

The post-cleanup macOS stack sample established a codec boundary before this
candidate was attempted. In the host process, the `ShareMeWorker` playback
thread appeared in approximately 1,135 of 1,401 collected samples under
`FfmpegMediaSource::read_next`; its child stacks included FFmpeg
`libswscale` and `libavcodec` HEVC work. Qt sink submission appeared only as a
small application-layer portion of the sample. The sample files remain
ignored local evidence and are not committed.

The candidate then enabled VideoToolbox only for HEVC when `auto` is selected,
reported `path=hardware` in host counters, and fell back to software for the
generated unsupported mpeg4 fixture. It preserved the 3840×2160 dimensions and
recorded zero coalescing in all six measured host/viewer roles. The candidate
executable SHA-256 is
`2c8c28968a68180f49cebb27849fb98e672937eb7d46285b44bfcb64502ed3e2`.
The raw candidate artifacts remain in an ignored output directory:

```text
run-01.jsonl  9061b3fbe30d0bd14adca5f3cbd06f04796327594307b114c36dcdfe0cea8a69
run-02.jsonl  8de7f8b890a2b18fc0a86d95e5c6c61942c9029944868e93916d8bd94dc3dde8
run-03.jsonl  66f7ab8bf524f2898de18d5c795069cab13d095d511facc4fe3ae722bbfa9fe9
```

| Run | Host average CPU | Viewer average CPU | Host CPU P95 | Viewer CPU P95 | Host RSS P95 | Viewer RSS P95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 01 | 112.13% | 23.40% | 154.0% | 33.2% | 660,799,488 | 266,649,600 |
| 02 | 110.80% | 23.29% | 151.0% | 33.6% | 835,452,928 | 289,521,664 |
| 03 | 113.38% | 23.97% | 154.6% | 33.5% | 823,443,456 | 289,636,352 |

| Gate | Result | Evidence |
| --- | --- | --- |
| Complete 180-second lifecycle | verified | 3/3 artifacts complete; each has 358 counter records and failure is null |
| Exact dimensions and metadata | failed | host pixel aspect remained 1/1 versus baseline 0/1; viewer dimensions differed from baseline in runs 01 and 02 |
| Cadence ≥99% of baseline | failed | minimum per-run/per-role submitted ratio was 70.20% in run 02 viewer |
| No additional drops/coalescing | verified | all candidate roles reported dropped=0 and coalesced=0; max_pending=1 |
| PSNR ≥45 dB | failed / missing | no same-timestamp decoded-frame pair was recorded |
| SSIM ≥0.995 | failed / missing | no same-timestamp decoded-frame pair was recorded |
| Combined average CPU reduction ≥30% | verified / insufficient | median-of-three reduction was 30.04%, but the candidate fails quality gates |
| CPU P95 non-regression | verified | combined candidate P95 change was -125.1 percentage points |
| RSS P95 growth ≤10% | verified | median-of-three growth was -20.58% |
| Paused CPU reduction ≥70% | failed / missing | dedicated paused probe was not executed |
| One-frame GUI backlog bound | verified / application-layer | all six role/run summaries reported max_pending=1; no display scanout proof |
| Preview/audio/pause/seek human checks | environment-dependent | no human GUI/audio confirmation was captured |

This hardware candidate is therefore **partial evidence**, not a verified
performance-and-quality result. Its CPU reduction cannot be traded for the
cadence regression, and it must not be merged or enabled as an accepted
quality-preserving stage until the full quality contract passes.

## Verification boundary

Verified on macOS: focused C++/Python contracts, direct-I420 media tests,
preview-adapter test, playback demo build, 3×180-second software baseline, and
3×180-second hardware-path candidate lifecycle. Windows is
environment-dependent and unverified. Full-suite, lifecycle-repeat, Go,
workflow, validator, final read-only review, and human GUI/audio acceptance
are recorded separately at the stage handoff and are not inferred from this
measurement document. Qt sink submission remains application-layer evidence
only; it does not verify display scanout, acoustic A/V synchronization, or
physical screen presentation.

## Stage verification

The completed macOS verification checkpoint passed full CTest 49/49,
including the standard-C++ portability regression contract, 20/20 repeated
`signaled_peer` lifecycles, `go test -race ./...`, `go vet
./...`, ShareMe Sol–Terra workflow tests 8/8, the ShareMe skill validator, and
`git diff --check`. These checks establish regression and tooling health; they
do not turn the failed performance gate into a pass. No Windows build, native
Windows media run, human preview/audio confirmation, display scanout, acoustic
A/V, or physical thermal measurement was performed.
