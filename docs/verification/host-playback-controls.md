# Host Playback Controls Verification

Date: 2026-08-03 (Asia/Shanghai)

## Delivered scope

The `codex/host-playback-controls` stage adds host-authoritative movie controls:

- one thread-safe `MovieTimeline` owns absolute media PTS, playing/paused state,
  duration, revision, and seek generation;
- independent movie video and movie audio sessions pause, resume, and seek from
  that timeline while retaining their existing queue policies and lifecycles;
- every accepted seek increments the shared generation once, clears each
  decoder session's queued frames, and recreates the audio PCM chunker;
- forward and backward seeks preserve a monotonic WebRTC transport timestamp
  while allowing the media PTS to move in either direction;
- the host Qt controller publishes the real timeline state, absolute PTS, and
  generation over `playback-state` and exposes bounded QML pause/resume/seek
  controls;
- builds without optional MovieRTC support still compile and require the CLI to
  reject movie mode explicitly.

This stage does not add viewer-authoritative controls, generation-aware viewer
playout buffering, playout reports, hard-resync commands, receiver speaker
playout, TURN operation, or public-network acceptance.

## macOS automated verification

The final feature code used the repository-external Darwin arm64 libwebrtc
cache read-only at `/Users/dio/Library/Caches/ShareMe/webrtc`.

The complete movie-call configuration passed:

```text
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev --output-on-failure
100% tests passed, 0 tests failed out of 38
```

The new coverage includes timeline overflow/state/stop behavior, paused video
and audio delivery, forward and backward seeks, generation changes, A/V track
offsets, playback-state generation, bounded QML controls, and the existing
FFmpeg/WebRTC/signaling regressions.

The optional-feature boundary was also exercised with MovieRTC disabled:

```text
cmake --preset call-dev -DWEBRTC_ROOT=/Users/dio/Library/Caches/ShareMe/webrtc
cmake --build --preset build-call-dev
ctest --preset test-call-dev --output-on-failure
100% tests passed, 0 tests failed out of 15
```

This exposed and repaired an existing CLI-test assumption so a build without
MovieRTC now tests explicit rejection instead of expecting unavailable movie
support.

## Integration review

The final direct review covered timeline lock ownership and checked arithmetic,
stop-token wakeups, decoder generation changes, queue discard, PCM chunk reset,
backward-seek transport timestamps, optional MovieRTC compilation, Qt object
lifetime, and Git scope. It found and repaired three material edge cases:

- RTP timestamps previously followed media PTS and could regress on a backward
  seek; they now follow the monotonic WebRTC capture clock;
- a media source starting after the timeline had advanced could open at the
  file start; it now seeks to the current shared snapshot before playback;
- a frame just beyond a container's nominal duration could wait forever at the
  clamped end; end-of-timeline now makes the wait due.

No Critical or Important review finding remains after the final 15/15 and
38/38 reruns.

The signaling server and repository workflow gates passed:

```text
go test -count=1 -race ./...
go vet ./...
python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py
# 8 tests, OK
python3 scripts/validate_shareme_skill.py
# Skill is valid!
```

`git diff --check` also passed. Generated build output remained ignored and no
cache content was staged.

## Evidence boundaries

- **Verified:** macOS compilation and automated behavior described above.
- **Partial:** QML is compiled and its control bindings have automated source
  contracts, but this run does not claim a human visual GUI acceptance.
- **Environment-dependent:** Windows must pull the merged stage and rerun its
  native movie-call build, CTest, and real GUI/media acceptance.
- **Unimplemented:** viewer hard resync and receiver speaker playout remain
  separate stages because the receiver does not yet have generation-aware
  playout/reporting infrastructure.
