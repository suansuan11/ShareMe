# Movie Audio Transport Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move injected movie PCM onto a dedicated PeerConnection so voice and movie audio remain independent without BUNDLE codec collisions or concurrent `AudioSendStream` producers.

**Architecture:** The existing `SignaledPeer` remains the primary video/voice/control connection. A new `MovieAudioPeer` owns a separate runtime, disables ADM recording, negotiates one stereo Opus audio m-line, and uses separate relay message types in the same signaling room.

**Tech Stack:** C++20, libwebrtc native API, Qt 6, FFmpeg, CMake/CTest, Go signaling.

## Global Constraints

- Preserve independent movie audio, host voice, and viewer voice lifecycles.
- Never mix voice and movie audio or disable primary voice as a workaround.
- Keep candidate staging bounded at 64 and sanitize failures without paths or raw SDP.
- Preserve existing video, playback timeline, DataChannel, and queue contracts.
- Do not modify the repository-external libwebrtc checkout/cache.
- Use separate relay types `movie-audio-session-description` and `movie-audio-ice-candidate`.
- macOS evidence does not verify Windows; receiver speaker playout remains out of scope.

---

### Task 1: Dedicated movie-audio PeerConnection

**Files:**
- Create: `client/rtc/webrtc/include/shareme/rtc/movie_audio_peer.hpp`
- Create: `client/rtc/webrtc/src/movie_audio_peer.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Create: `tests/rtc/movie_audio_peer_test.cpp`
- Modify: `tests/rtc/CMakeLists.txt`

**Interfaces:**
- Produces: `MovieAudioPeerConfig`, `MovieAudioPeerResult`, `MovieAudioPeerCallbacks`, and `MovieAudioPeer`.
- Consumes: `SignaledRole`, `LocalAudioSourceFactory`, `CandidateStager`, `WebRtcRuntime`, and `CountingAudioSink`.

- [x] **Step 1: Write the failing configuration and negotiation test**

Require these contracts:

```cpp
using shareme::rtc::MovieAudioPeer;
using shareme::rtc::MovieAudioPeerConfig;
REQUIRE(MovieAudioPeer::create(
            MovieAudioPeerConfig{.role = SignaledRole::host}, {}) == nullptr);
REQUIRE(MovieAudioPeer::create(
            MovieAudioPeerConfig{.role = SignaledRole::viewer,
                                 .source_factory = fake_factory}, {}) == nullptr);
```

Create paired host/viewer peers with callbacks that relay descriptions and
candidates. The host fake source emits stereo 48 kHz, 480-frame PCM callbacks
from one jthread. Require a connected result, at least 100 valid viewer
callbacks, zero invalid callbacks, two channels, and exactly one source stop.
Serialize the offer and require one `m=audio`, `stereo=1`, and
`sprop-stereo=1`.

- [x] **Step 2: Run RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_audio_peer_test
```

Expected: target/type missing.

- [x] **Step 3: Implement the dedicated peer**

Define:

```cpp
struct MovieAudioPeerConfig {
  SignaledRole role{SignaledRole::host};
  LocalAudioSourceFactory source_factory;
};
struct MovieAudioPeerResult {
  bool connected{};
  std::uint64_t frames_received{};
  std::uint64_t invalid_frames_received{};
  int sample_rate{};
  int channels{};
  int peak{};
  std::uint64_t chunks_generated{};
  std::string selected_candidate_type;
  std::string error;
};
```

Use the existing callback shapes. Create a separate runtime and PeerConnection;
call `peer_->SetAudioRecording(false)` during setup. The host creates only the
custom `movie-audio` track; the viewer adds no local track. Apply stereo Opus
fmtp only to this single-audio-m-line connection. Stage at most 64 candidates.
Make `stop()` idempotent, stop/join the source before clearing the track on the
signaling thread, and cancel/join waiters before destruction.

- [x] **Step 4: Run GREEN and repeat lifecycle coverage**

```bash
cmake --build --preset build-movie-call-dev --target shareme_movie_audio_peer_test
ctest --preset test-movie-call-dev -R '^movie_audio_peer$' --repeat until-fail:20 --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add client/rtc/webrtc tests/rtc
git commit -m "feat: isolate movie audio peer connection"
```

### Task 2: Remove injected audio from the primary peer

**Files:**
- Modify: `client/rtc/webrtc/include/shareme/rtc/signaled_peer.hpp`
- Modify: `client/rtc/webrtc/src/signaled_peer.cpp`
- Modify: `tests/rtc/signaled_peer_test.cpp`

**Interfaces:**
- Consumes: Task 1 `MovieAudioPeer` as the replacement owner.
- Produces: a primary `SignaledPeer` that owns only video, voice, and control.

- [ ] **Step 1: Replace the old positive stereo-munging test with a failing isolation test**

Remove the assertion that primary voice and movie m-lines intentionally use
different PT 111 parameters. Require `SignaledPeerConfig` to have no
`movie_audio_source_factory`, primary offers to contain no `movie-audio` track,
and existing bidirectional voice/video/control tests to remain unchanged.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_signaled_peer_test
```

Expected: compilation fails while the old config field and ownership remain.

- [ ] **Step 3: Delete primary movie-audio ownership**

Remove the config field, source/track/sink members, delayed enabling, stereo SDP
mutation, movie-audio result population, and movie-audio-specific wait checks.
Do not modify voice track options, video callbacks, DataChannel behavior,
candidate staging, or primary shutdown serialization.

- [ ] **Step 4: Run primary and dedicated peer tests**

```bash
cmake --build --preset build-movie-call-dev --target shareme_signaled_peer_test shareme_movie_audio_peer_test
ctest --preset test-movie-call-dev -R '^(signaled_peer|movie_audio_peer)$' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add client/rtc/webrtc tests/rtc/signaled_peer_test.cpp
git commit -m "refactor: keep primary peer audio voice-only"
```

### Task 3: Wire two peers into CLI and GUI, then close the regression

**Files:**
- Modify: `client/tools/signaled_call/main.cpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.hpp`
- Modify: `client/tools/rtc_demo/rtc_demo_controller.cpp`
- Modify: `tests/scripts/signaled_call_cli_test.py`
- Modify: `tests/scripts/rtc_demo_cli_test.py`
- Modify: `scripts/run_signaled_call_smoke.py`
- Modify: `docs/development/current-stage.md`
- Create: `docs/verification/movie-audio-isolation.md`

**Interfaces:**
- Consumes: Task 1 `MovieAudioPeer` and Task 2 primary `SignaledPeer`.
- Produces: disjoint primary/movie relay routing in one room and end-to-end evidence.

- [ ] **Step 1: Add failing relay and stderr contracts**

Require the probe/controller sources and runtime path to use exactly:

```text
movie-audio-session-description
movie-audio-ice-candidate
```

Extend the smoke orchestrator's captured child stderr check so movie-audio
acceptance fails with `SMOKE_ERROR smoke-failed` when it contains either
`codec collision` or `RaceDetected`. Keep paths and captured logs out of the
displayed error.

- [ ] **Step 2: Run RED**

```bash
cmake --build --preset build-movie-call-dev --target shareme_signaled_call_probe shareme_rtc_demo
ctest --preset test-movie-call-dev -R '^(signaled_call_cli_contract|rtc_demo_cli_contract|signaled_call_smoke_contract)$' --output-on-failure
```

Expected: relay/static contracts fail because only primary message types exist.

- [ ] **Step 3: Integrate two peer lifecycles**

Keep primary relay routing unchanged. Route dedicated descriptions/candidates
through the two exact movie relay types. Host creates the dedicated peer only
when `--movie-audio` is enabled; viewers create a receive-side dedicated peer.
Start both when the room participant is ready. On shutdown cancel and join both
waiters, stop the movie peer before releasing its timeline/source, then stop the
primary peer. A movie peer failure reports `movie-audio-*` but does not destroy
an already connected primary call.

Aggregate movie metrics from `MovieAudioPeerResult` into the existing stable
probe `RESULT` fields. Do not change CLI flags or QML control behavior.

- [ ] **Step 4: Run focused GREEN and repeated real smoke**

```bash
cmake --build --preset build-movie-call-dev --target shareme_signaled_call_probe shareme_rtc_demo
ctest --preset test-movie-call-dev -R '^(movie_audio_peer|signaled_peer|signaled_call_cli_contract|rtc_demo_cli_contract|signaled_call_smoke_contract)$' --output-on-failure
for port in 18251 18252 18253 18254 18255; do
  python3 scripts/run_signaled_call_smoke.py \
    --probe build/movie-call-dev/client/tools/signaled_call/shareme_signaled_call_probe \
    --server-root server --port "$port" --audio microphone --video movie \
    --movie-audio --movie build/movie-call-dev/tests/rtc/generated-movie-call.mp4
done
```

- [ ] **Step 5: Run complete stage verification**

```bash
cmake --build --preset build-movie-call-dev
ctest --preset test-movie-call-dev --output-on-failure
(cd server && go test -count=1 -race ./... && go vet ./...)
python3 -m unittest tests/workflow/shareme_sol_luna_workflow_test.py
python3 scripts/validate_shareme_skill.py
git diff --check
```

- [ ] **Step 6: Document, commit, and hand back to Sol**

Record automated macOS evidence separately from the pending manual GUI rerun
and Windows-native rerun. Keep receiver speaker playout unimplemented.

```bash
git add client/tools scripts tests/scripts docs
git commit -m "fix: route movie audio on isolated transport"
```
