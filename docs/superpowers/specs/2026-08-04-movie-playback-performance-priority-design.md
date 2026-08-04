# Movie Playback Quality-Preserving Performance Design

Status: approved by the user's explicit request to make playback heat the
highest-priority item. The user additionally requires no reduction in viewing
experience.

## Priority and problem

Playing the supplied 4K HEVC/FLAC movie causes severe user-observed heating on
an Apple M4 MacBook Air. This work supersedes viewer reportability and
hard-resync. Hard-resync remains unimplemented and the drift gate stays paused.

Current source inspection shows a high-confidence but unverified hot-path
hypothesis:

1. FFmpeg decodes 4K HEVC and `sws_scale` converts every frame to BGRA.
2. `MovieVideoSource` converts BGRA back to I420 for WebRTC.
3. WebRTC software codecs encode, transport, and decode the full-resolution
   stream.
4. Host and viewer preview callbacks call `ToI420`, allocate a full-frame ARGB
   `QImage`, convert I420 to ARGB, and submit the copied frame to Qt.

Repeated 4K color conversion, allocation, memory copies, and software codec
work can plausibly cause high CPU and memory bandwidth. Profiling must establish
the actual cost before implementation claims a root cause.

## Immutable quality contract

Performance must not be purchased by lowering viewing quality. Candidate builds
must preserve:

- source and transmitted dimensions;
- source frame cadence and presentation timestamps;
- pixel/sample aspect ratio, color range, and color-space metadata;
- existing WebRTC codec and encoder bitrate/quality configuration;
- baseline-or-better submitted-frame and dropped-frame behavior;
- movie-audio format, continuity, and audible output;
- sender and receiver preview behavior; and
- pause/resume, seek, generation, and synchronization semantics.

No downscaling, frame-rate cap, bitrate reduction, lower codec quality, chroma
degradation, or new intentional frame dropping is allowed.

For deterministic sampled timestamps, the candidate must have PSNR at least
45 dB and SSIM at least 0.995 versus the software baseline, in addition to exact
geometry and metadata checks. Human viewing and listening confirmation remains
required because numerical similarity is not complete viewing proof.

## Goals

- Establish reproducible host/viewer CPU, RSS, frame-path, and quality evidence.
- Remove redundant BGRA/I420 conversions and avoid per-frame ARGB allocation
  where ownership contracts allow planar YUV submission.
- Use quality-preserving VideoToolbox on macOS and D3D11VA/Media Foundation on
  Windows where profiling demonstrates codec cost.
- Reduce combined host/viewer average CPU by at least 30 percent against the
  median same-machine software baseline.
- Keep deterministic software fallback and expose the selected path.

## Non-goals

- No hard-resync, drift threshold, audio, voice, signaling, or room redesign.
- No claim that lower CPU proves lower physical temperature without supported
  energy or thermal evidence.
- No committed movies, JSONL, traces, logs, build output, local paths, or cache.
- No modification or deletion of the external libwebrtc cache.

## Selected approach

Three approaches were considered:

1. Lower resolution/frame rate/bitrate: simple but rejected because it degrades
   viewing experience.
2. Conversion cleanup only: low risk and mandatory, but may leave 4K HEVC and
   WebRTC software codec CPU high.
3. Measurement, conversion cleanup, then evidence-gated hardware acceleration:
   selected because it preserves fidelity while addressing copies and codecs.

Platform adapters must remain isolated. Portable quality comparison and
counters stay in `client/core`; FFmpeg, Qt, WebRTC, VideoToolbox, and Windows
APIs remain outside it.

## Measurement contract

### Scenario

- Use the supplied `01.mkv`; committed docs use `<MOVIE_PATH>`.
- Launch one local signaling server, one movie host with movie audio, and one
  viewer.
- Use a 180-second run: 30-second warmup, 120-second measurement, 30-second
  finalization.
- Run three sequential forced-software baselines and three sequential
  automatic-acceleration candidates on the same machine, power mode, display
  arrangement, build type, media, and topology.
- Record macOS and Windows independently.

### Metrics

Ignored once-per-second JSONL records only sanitized fields:

- monotonic sample time, process role, CPU percentage, and RSS;
- decoded, offered, encoded/received when available, callback, submitted,
  coalesced/dropped, and conversion-failure counts;
- dimensions, cadence, pixel aspect ratio, color metadata, and selected codec
  or acceleration path;
- audio, RTC, decode, fallback, and finalization failures; and
- playing/paused state and selected ICE candidate type.

Committed evidence contains aggregate summaries, hashes, sanitized commands,
tool versions, and platform scope. `xctrace` Time Profiler/Energy Log and
Windows WPA/ETW traces remain outside Git and are optional diagnostics.

### Gates

Every candidate run must satisfy:

- complete 180-second lifecycle without call, decode, RTC, audio, or artifact
  failure;
- exact baseline dimensions, aspect ratio, color metadata, codec configuration,
  and timestamp behavior;
- submitted cadence at least 99 percent of baseline and no additional dropped
  or coalesced frames;
- PSNR >=45 dB and SSIM >=0.995 at the frozen sampled timestamps;
- both previews visible, audio audible, and pause/resume plus forward/backward
  seek confirmed;
- combined average CPU at least 30 percent below the same-machine median
  baseline, CPU P95 no worse than baseline, and RSS P95 growth <=10 percent;
- dedicated paused-probe CPU at least 70 percent below active playback; and
- one-frame GUI delivery bound with no unbounded backlog.

Missing quality evidence fails the gate. If CPU misses its target, preserve the
result and optimize another quality-preserving codec/copy boundary; never tune
the gate or reduce quality.

## Architecture

### Quality contract

Add Qt-free `VideoQualityContract` and `QualityComparison` types covering
geometry, rational cadence, pixel aspect ratio, color metadata, codec identity,
PSNR, SSIM, and drop counts. Invalid or missing metrics cannot pass.

### Direct I420 media path

Represent planar I420 in the media frame contract and configure FFmpeg to
produce I420 directly when the decoded format requires conversion. Preserve
source dimensions and metadata. `MovieVideoSource` consumes those planes
without the BGRA-to-I420 round trip. Unsupported formats use one observable,
tested conversion fallback.

### Qt YUV preview adapter

Move preview work out of `RtcDemoController`. A focused adapter holds a
ref-counted WebRTC buffer and exposes planar YUV through a `QVideoFrame` mapping
compatible with Qt's lifetime rules. If the platform cannot map it, use one
bounded ARGB-copy fallback and report that path. Do not add throttling. Viewer
playout reporting occurs only after successful sink submission.

### Hardware adapters

Add `--video-acceleration auto|software`; `auto` is default only after quality
validation. VideoToolbox and D3D11VA/Media Foundation live behind platform
guards with deterministic software fallback. Implement only the decode or
encode boundary shown dominant by the baseline. Selected and fallback paths are
observable but contain no device identifiers.

## Delivery stages

1. Baseline runner, workload/quality counters, and three software baselines.
2. Direct-I420 media path and ref-counted Qt YUV preview with fallback.
3. Re-profile; add only evidenced macOS/Windows hardware codec adapters.
4. Three candidate runs, objective and human quality gates, regression suite,
   review, and handoff.

The feature uses an ignored worktree and focused commits. Luna may push the
feature branch after verification but must leave `main` unchanged for Sol
review and user-authorized merge.
