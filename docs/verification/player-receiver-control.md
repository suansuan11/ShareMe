# Player / Receiver Control Verification

Date: 2026-08-03 (Asia/Shanghai)

## Delivered scope

Implementation commit `92060e5` adds the first host-authoritative receiver
control slice:

- `shareme_rtc_demo` accepts a host movie source and optional independent movie
  audio source;
- the host creates a reliable ordered `shareme-control-v1` WebRTC data channel;
- the viewer validates and tracks version-1 `playback-state` snapshots;
- the receiver QML displays the last accepted host state and media position;
- state snapshots use the last emitted source media PTS, including non-zero
  container start offsets; EOF is published once as `paused` at the final PTS;
- Windows movie paths use native wide-character filesystem conversion.

This is a read-only state channel. Viewer-authoritative pause/seek, playout
reports, clock correction, remote speaker playout, TURN, and public-network
acceptance are not implemented by this stage.

## Automated verification

The final feature-branch code passed on macOS:

```text
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev --output-on-failure
100% tests passed, 0 tests failed out of 37
```

The 37 tests include a real in-process two-`SignaledPeer` negotiation and
reliable data-channel message round trip, playback-state schema/tracker
boundaries, the RTC demo movie CLI contract, FFmpeg playback/movie sources, and
existing WebRTC regressions.

The signaling service also passed:

```text
go test -count=1 -race ./...
go vet ./...
```

Repository workflow checks passed 7/7 and the repository skill validator
reported `Skill is valid!`.

## Review

A Sol high-reasoning read-only review initially found five Important issues and
one Minor issue. The implementation was repaired to propagate data-channel send
failure, require reliability, enforce globally monotonic sequence numbers,
validate JSON-safe integers and room grammar, preserve Windows Unicode paths,
and reject invalid buffers before allocation. The same reviewer approved the
final diff with no remaining findings.

The Luna implementer was explicitly requested as `gpt-5.6-terra` with medium
reasoning. Runtime telemetry did not expose the actual selected model, so model
override acceptance and realized credit savings are not claimed.

## Local process acceptance

The Go service, movie host, and viewer were launched locally and both clients
reported joining the same generated room. The processes remained healthy until
they were deliberately terminated.

Visual GUI acceptance is **not verified** in this run: the Codex system capture
returned a black image and Computer Use could not address the standalone Qt
executable because it has no discoverable app bundle. Therefore this evidence
does not claim that a human observed the receiver frame or state label.

Windows must rerun the affected native build/tests and GUI acceptance. The
external libwebrtc cache under `/Users/dio/Library/Caches/ShareMe/webrtc` was
used read-only and was not cleaned or modified.
