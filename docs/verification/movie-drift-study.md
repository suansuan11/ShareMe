# Movie Drift Study Verification

Status: **partial — measurement gate failed before a three-run aggregate**.
Automatic bounded hard-resync remains **unimplemented**. Tasks 7–10 were not
started.

## Scope and build

This stage started from `main` at `0dbad076df9179e5d3dc9127f8c6b3b68a1f912a`
and ran in the isolated `codex/drift-hard-resync` worktree. The measurement
tooling was built at `5c0503421d0dbff5652bf5781c54737a47dc1cbb`.

The required media was used without copying or modifying it. Committed
evidence names it as `<MOVIE_PATH>` and does not contain a machine-local media
path. The media probe reported a 1,421.045-second 4K HEVC video and 48 kHz
stereo FLAC audio, so the profile's 330-second remaining-duration precondition
was satisfied.

The measured platform was macOS Darwin arm64 on an Apple M4 MacBook Air,
macOS 26.6. The Qt version was 6.11.1, FFmpeg 8.1.1, CMake 4.3.3, Ninja
1.13.2, Go 1.26.5, and Python 3.9.6. The external libwebrtc manifest revision
was `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`; the external cache was read
only and was not cleaned, rebuilt, modified, or staged.

## Procedure and artifacts

The runner was invoked with the required profile, fixed `--run-count 3`, and
the supplied movie. The output root was ignored under `build/`. The formal
run was stopped by the runner after its first gate-eligible run failed; no
second or third formal run was started. Two additional single-run diagnostics
were used only to isolate the failure: one with Qt `offscreen`, and one with
the native macOS Cocoa platform. They were not counted as formal runs.

The final JSONL artifacts contain only a summary record: `acceptedSamples=0`,
`rejectedSamples=0`, and no sample records. Each host result also reported
`received_reports=0`. All three artifacts were finalized atomically and have
the same SHA-256 because the same empty-summary result was reproduced:

| Artifact | Bytes | SHA-256 | Role |
| --- | ---: | --- | --- |
| `build/drift-study/run-01.jsonl` | 677 | `baf158fffe7838e15c1925f1c97458f1fb9a6c79140a9c1cfa1029eed020087c` | formal run 1 |
| `build/drift-study-diagnostic/run-01.jsonl` | 677 | `baf158fffe7838e15c1925f1c97458f1fb9a6c79140a9c1cfa1029eed020087c` | offscreen diagnosis |
| `build/drift-study-native/run-01.jsonl` | 677 | `baf158fffe7838e15c1925f1c97458f1fb9a6c79140a9c1cfa1029eed020087c` | native Cocoa diagnosis |

The repeated `received_reports=0` result locates the failure at or before a
valid `PlayoutReport` arriving at the host. The current sanitized host-side
counter cannot distinguish a viewer sink callback failure from a viewer
DataChannel send failure, so neither is claimed as the sole root cause. This
is application-layer evidence only; it does not verify display scanout,
acoustic A/V synchronization, or a physical screen presentation.

## Measurement gate

The frozen gate from the design was applied without changing thresholds:

| Gate | Result | Evidence |
| --- | --- | --- |
| At least 900 accepted reports outside pause for every run | **FAIL** | 0 accepted reports in the formal run |
| No accepted sequence or generation regression | **FAIL — no data** | No accepted samples exist from which to establish this condition |
| No non-pause report gap over 2 seconds | **FAIL — no data** | No report stream exists |
| Steady/post-recovery absolute P99 below 300 ms | **FAIL — no data** | No percentile can be computed |
| At least 95% of steady/post-recovery samples within 100 ms | **FAIL — no data** | No eligible samples exist |
| Resume and both seeks recover in three reports within 5 seconds | **FAIL — no data** | No recovery sequence exists |
| No captured call/decode/RTC/native-audio failure | **PARTIAL** | No failure category was captured, but absent reports prevent a complete media-path certification |

Because the first formal run failed the required accepted-report gate, the
three-run aggregate was not eligible to pass. The absence of a report stream
is not reinterpreted as a successful zero-drift result.

## Verification boundaries

- **Verified:** portable drift aggregation, sanitized JSONL schema and atomic
  finalize tests; deterministic five-minute scheduler and bounded seek tests;
  fixed host-only CLI contract; runner unit contracts; macOS build and CTest
  44/44; Go race tests and vet; Sol–Terra workflow 8/8; skill validator; and
  `git diff --check`.
- **Partial:** macOS real-media session lifecycle and scenario completion. The
  supplied movie opened sufficiently for the scripted host scenario to reach
  its 300-second completion, but no valid viewer playout reports reached the
  host. The formal three-run measurement set is therefore not complete.
- **Environment-dependent:** Windows was not executed. TURN/public-network,
  physical scanout, acoustic A/V, and cross-machine synchronization remain
  unverified.
- **Unimplemented:** sync-command codec/tracker, automatic hard-resync policy,
  opt-in hard-resync integration, and the 20-run lifecycle repetition for the
  new correction path. No production backdoor or artificial offset injection
  was added.

## Reproduction command

Use placeholders for machine-local roots and the required movie path:

```sh
cmake --fresh --preset movie-call-dev \
  -DWEBRTC_ROOT=<WEBRTC_ROOT> \
  -DCMAKE_PREFIX_PATH=<QT_PREFIX>
cmake --build --preset build-movie-call-dev --parallel 4
python3 scripts/run_movie_drift_study.py \
  --demo <DEMO_PATH> \
  --server-url ws://127.0.0.1:18080/v1/ws \
  --server-root <SHAREME_ROOT>/server \
  --movie <MOVIE_PATH> \
  --output-parent <BUILD_ROOT> \
  --output-root <BUILD_ROOT>/drift-study \
  --run-count 3 \
  --timeout-seconds 390
```

The runner never deletes pre-existing artifacts and refuses an output root
outside the explicit output parent. Raw JSONL remains ignored and must not be
staged.
