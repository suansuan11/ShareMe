# WebRTC Test Media Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a headless native-libwebrtc loopback demonstration that sends generated VP8 video and synthetic or physical-microphone Opus audio, receives both tracks, and reports verifiable ICE, DTLS, RTP, media, and shutdown evidence.

**Architecture:** Portable probe contracts and bounded ICE staging remain in `client/core/rtc`. An optional `client/rtc/webrtc` adapter owns all libwebrtc threads and objects, while a thin CLI runs an encrypted two-PeerConnection loopback and prints one sanitized JSON result. The locked external WebRTC checkout and GN output remain outside Git.

**Tech Stack:** C++20, CMake 3.25+, Python 3 standard library, Chromium depot_tools/GN/Ninja, native libwebrtc at revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97`, CTest

---

### Task 1: Lock and Bootstrap the External WebRTC Dependency

**Files:**
- Create: `deps/webrtc.lock.json`
- Create: `scripts/bootstrap_webrtc.py`
- Create: `tests/scripts/bootstrap_webrtc_test.py`
- Modify: `.gitignore`

- [x] **Step 1: Write failing bootstrap metadata tests**

Create `tests/scripts/bootstrap_webrtc_test.py` with standard-library
`unittest` cases that:

```python
class BootstrapWebRtcTest(unittest.TestCase):
    def test_lock_uses_full_expected_revision(self):
        lock = load_lock(REPO / "deps/webrtc.lock.json")
        self.assertEqual(
            lock["revision"],
            "5ad58d70eea10785fab05ba4150e2fe22ecc7f97",
        )
        self.assertEqual(
            lock["targets"],
            ["webrtc", "modules/audio_device:test_audio_device_module"],
        )

    def test_plan_keeps_checkout_outside_repository(self):
        with self.assertRaisesRegex(ValueError, "outside the repository"):
            create_plan(REPO, REPO / ".cache")

    def test_manifest_records_abi_inputs(self):
        manifest = make_manifest(
            revision=EXPECTED_REVISION,
            system="Darwin",
            architecture="arm64",
            include_dir="/external/src",
            libraries=["/external/out/obj/libwebrtc.a"],
            compile_definitions=["WEBRTC_POSIX", "WEBRTC_MAC"],
            gn_args=["is_debug=false"],
        )
        self.assertEqual(manifest["revision"], EXPECTED_REVISION)
        self.assertEqual(manifest["architecture"], "arm64")
        self.assertTrue(manifest["libraries"])
```

Import the planned functions from `scripts/bootstrap_webrtc.py`.

- [x] **Step 2: Run the tests and observe RED**

Run:

```bash
python3 -m unittest tests/scripts/bootstrap_webrtc_test.py -v
```

Expected: import failure because `scripts/bootstrap_webrtc.py` does not exist.

- [x] **Step 3: Add the lock and minimum bootstrap implementation**

Create `deps/webrtc.lock.json`:

```json
{
  "revision": "5ad58d70eea10785fab05ba4150e2fe22ecc7f97",
  "targets": [
    "webrtc",
    "modules/audio_device:test_audio_device_module"
  ],
  "gnArgs": [
    "is_debug=false",
    "is_component_build=false",
    "rtc_build_examples=false",
    "rtc_include_tests=false",
    "rtc_use_h264=false",
    "use_rtti=true"
  ]
}
```

Implement `load_lock`, `create_plan`, `make_manifest`, platform archive
resolution, subprocess execution, and these CLI modes:

```text
bootstrap_webrtc.py --root <external-path> --print-plan
bootstrap_webrtc.py --root <external-path> --prepare
bootstrap_webrtc.py --root <external-path> --build
```

`--prepare` clones `depot_tools`, runs `fetch --nohooks webrtc`, checks out the
locked revision, and runs `gclient sync`. `--build` runs `gn gen`, builds both
locked targets with `autoninja`, validates required headers and archives, then
writes `<root>/shareme-webrtc-manifest.json` atomically.

Reject a root equal to or below the repository directory. Never delete an
existing checkout automatically. Redact checkout URLs and local paths from
raised user-facing diagnostics.

Add only generic accidental in-repository names to `.gitignore`:

```gitignore
/depot_tools/
/webrtc-checkout/
/shareme-webrtc-manifest.json
```

- [x] **Step 4: Verify GREEN**

Run:

```bash
python3 -m unittest tests/scripts/bootstrap_webrtc_test.py -v
python3 scripts/bootstrap_webrtc.py \
  --root /Users/dio/Library/Caches/ShareMe/webrtc \
  --print-plan
git diff --check
```

Expected: all Python tests pass; the plan lists the locked revision, two GN
targets, and an external root without downloading anything.

- [x] **Step 5: Commit dependency policy**

```bash
git add .gitignore deps/webrtc.lock.json scripts/bootstrap_webrtc.py tests/scripts/bootstrap_webrtc_test.py
git commit -m "build: lock external libwebrtc dependency"
```

### Task 2: Add Optional CMake Discovery Without Affecting Core Builds

**Files:**
- Create: `cmake/FindWebRTC.cmake`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`
- Modify: `.github/workflows/core-ci.yml`

- [x] **Step 1: Verify the option is currently inert**

Run:

```bash
cmake --fresh -S . -B build/webrtc-missing \
  -DSHAREME_ENABLE_WEBRTC=ON \
  -DWEBRTC_ROOT=/tmp/shareme-missing-webrtc
```

Expected before implementation: configuration succeeds because the existing
option is not connected to dependency discovery.

- [x] **Step 2: Implement strict manifest discovery**

`FindWebRTC.cmake` must:

1. require `WEBRTC_ROOT` or its environment equivalent;
2. read `shareme-webrtc-manifest.json` with `string(JSON ...)`;
3. compare the exact revision to `deps/webrtc.lock.json`;
4. compare the manifest system and architecture to the active CMake target;
5. require `api/peer_connection_interface.h`,
   `api/create_modular_peer_connection_factory.h`, and both static libraries;
6. create `WebRTC::webrtc` as an imported interface target with include paths,
   archives, compile definitions, thread support, and platform system
   frameworks/libraries;
7. expose `WebRTC_REVISION`.

Connect it in the root:

```cmake
if(SHAREME_ENABLE_WEBRTC)
  find_package(WebRTC REQUIRED)
endif()
```

Add `webrtc-dev`, `build-webrtc-dev`, and `test-webrtc-dev` presets. The preset
does not contain a machine path; developers provide `WEBRTC_ROOT`.

Extend Core CI path filters for `client/rtc/**`, `client/tools/**`, `deps/**`,
and `scripts/**`. Keep the CI build on default presets with WebRTC disabled.

- [x] **Step 3: Verify explicit failure and dependency-off behavior**

Run:

```bash
cmake --fresh -S . -B build/webrtc-missing \
  -DSHAREME_ENABLE_WEBRTC=ON \
  -DWEBRTC_ROOT=/tmp/shareme-missing-webrtc
cmake --fresh --preset dev
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure
```

Expected: the enabled configuration fails with a missing-manifest diagnostic;
the default build still passes 2/2 tests.

- [x] **Step 4: Commit build discovery**

```bash
git add CMakeLists.txt CMakePresets.json cmake/FindWebRTC.cmake .github/workflows/core-ci.yml
git commit -m "build: add optional libwebrtc discovery"
```

### Task 3: Define Portable Probe Contracts with TDD

**Files:**
- Create: `client/core/include/shareme/rtc/probe_contract.hpp`
- Create: `client/core/src/probe_contract.cpp`
- Create: `tests/core/probe_contract_test.cpp`
- Modify: `client/core/CMakeLists.txt`
- Modify: `tests/core/CMakeLists.txt`

- [ ] **Step 1: Write failing validation and serialization tests**

Test:

```cpp
using shareme::rtc::ProbeAudioMode;
using shareme::rtc::ProbeConfig;
using shareme::rtc::ProbeResult;
using shareme::rtc::ProbeStatus;

REQUIRE(validate(ProbeConfig{}).has_value() == false);
REQUIRE(validate(ProbeConfig{.width = 641}).has_value());
REQUIRE(validate(ProbeConfig{.frames_per_second = 61}).has_value());
REQUIRE(validate(
    ProbeConfig{.run_for = std::chrono::milliseconds{999}}).has_value());

ProbeResult result{
    .status = ProbeStatus::passed,
    .connection_time = std::chrono::milliseconds{125},
    .video_frames_sent = 90,
    .video_frames_received = 88,
    .audio_packets_sent = 150,
    .audio_packets_received = 149,
    .audio_level = 0.25,
    .selected_candidate_type = "host",
};
const auto json = to_json(result, "locked-revision", "Darwin", "arm64");
REQUIRE(json.find("\"status\":\"passed\"") != std::string::npos);
REQUIRE(json.find("\"videoFramesReceived\":88") != std::string::npos);
REQUIRE(json.find("\"audioLevel\":0.25") != std::string::npos);
```

Also verify JSON escaping, every status string, odd dimensions, zero duration,
and that diagnostics longer than 256 bytes are truncated.

- [ ] **Step 2: Run and observe RED**

Run:

```bash
cmake --build --preset build-dev --target shareme_probe_contract_test
```

Expected: target/header missing.

- [ ] **Step 3: Implement the portable contract**

Define the enums and structures exactly as the design specifies. Add:

```cpp
[[nodiscard]] std::optional<std::string> validate(const ProbeConfig& config);
[[nodiscard]] std::string_view to_string(ProbeAudioMode mode) noexcept;
[[nodiscard]] std::string_view to_string(ProbeStatus status) noexcept;
[[nodiscard]] std::string to_json(
    const ProbeResult& result,
    std::string_view revision,
    std::string_view platform,
    std::string_view architecture);
```

Use a local JSON-string escape helper; do not add a JSON dependency. Clamp the
diagnostic to 256 bytes before serialization.

- [ ] **Step 4: Verify and commit**

Run:

```bash
cmake --build --preset build-dev
ctest --preset test-dev -R probe_contract --output-on-failure
ctest --preset test-dev --output-on-failure
git add client/core tests/core
git commit -m "feat(rtc): add portable probe contracts"
```

Expected: 3/3 portable tests pass.

### Task 4: Add Bounded ICE Candidate Staging with TDD

**Files:**
- Create: `client/core/include/shareme/rtc/candidate_stager.hpp`
- Create: `tests/core/candidate_stager_test.cpp`
- Modify: `tests/core/CMakeLists.txt`

- [ ] **Step 1: Write failing bounded-staging tests**

Define test values with opaque payload strings and require:

```cpp
CandidateStager<std::string, 3> stager;
REQUIRE(stager.stage("one"));
REQUIRE(stager.stage("two"));
REQUIRE(stager.stage("three"));
REQUIRE_FALSE(stager.stage("four"));
REQUIRE(stager.overflow_count() == 1);

const auto drained = stager.drain();
REQUIRE(drained == std::vector<std::string>({"one", "two", "three"}));
REQUIRE(stager.size() == 0);
REQUIRE(stager.drain().empty());
```

Also test `clear`, move-only payloads, and that capacity zero is rejected at
compile time.

- [ ] **Step 2: Run and observe RED**

Run:

```bash
cmake --build --preset build-dev --target shareme_candidate_stager_test
```

Expected: header and target missing.

- [ ] **Step 3: Implement the header-only stager**

Use `std::array<std::optional<T>, Capacity>` with head and size indices. Reject
new entries after capacity is reached, increment a saturating overflow counter,
preserve FIFO drain order, and release stored values on `clear`.

The class owns no locks; its contract states that the WebRTC signaling thread
is the sole caller.

- [ ] **Step 4: Verify and commit**

```bash
cmake --build --preset build-dev
ctest --preset test-dev -R candidate_stager --output-on-failure
ctest --preset test-dev --output-on-failure
git add client/core/include/shareme/rtc/candidate_stager.hpp tests/core
git commit -m "feat(rtc): bound pending ICE candidates"
```

Expected: 4/4 portable tests pass.

### Task 5: Bootstrap and Validate the Locked Native Dependency

**Files:**
- Generated outside Git: `/Users/dio/Library/Caches/ShareMe/webrtc/**`
- Modify only if evidence requires a fix:
  `scripts/bootstrap_webrtc.py`, `deps/webrtc.lock.json`,
  `cmake/FindWebRTC.cmake`, `tests/scripts/bootstrap_webrtc_test.py`

- [ ] **Step 1: Run platform and storage preflight**

Run:

```bash
xcode-select -p
xcodebuild -version
df -h /Users/dio
python3 scripts/bootstrap_webrtc.py \
  --root /Users/dio/Library/Caches/ShareMe/webrtc \
  --print-plan
```

Expected on macOS: full Xcode is active and at least 20 GiB is available.
The current machine is known to have storage but only Command Line Tools; if
`xcodebuild` remains unavailable, record macOS bootstrap as environment-blocked
and run Steps 2–4 on the Windows development machine instead of weakening the
check.

- [ ] **Step 2: Prepare the external checkout**

macOS:

```bash
python3 scripts/bootstrap_webrtc.py \
  --root /Users/dio/Library/Caches/ShareMe/webrtc \
  --prepare
```

Windows PowerShell:

```powershell
py scripts/bootstrap_webrtc.py `
  --root "$env:LOCALAPPDATA\\ShareMe\\webrtc" `
  --prepare
```

Expected: checkout HEAD equals the locked revision and nothing appears in
`git status`.

- [ ] **Step 3: Build the two locked GN targets**

Use the same platform command with `--build`.

Expected: the aggregate `webrtc` archive, standalone
`test_audio_device_module` archive, required headers, and manifest exist under
the external root.

- [ ] **Step 4: Verify CMake accepts only the built manifest**

Run with the actual external root:

```bash
cmake --fresh --preset webrtc-dev \
  -DWEBRTC_ROOT=/Users/dio/Library/Caches/ShareMe/webrtc
```

Expected: exact revision, system, architecture, headers, and both archives are
reported as found. Temporarily pointing at a copied manifest with a changed
revision must fail.

- [ ] **Step 5: Commit only evidence-led bootstrap fixes**

If no tracked file changed, do not create an empty commit. Otherwise run the
Python tests, `git diff --check`, and commit only the bootstrap/discovery fix:

```bash
git commit -m "fix(build): validate locked libwebrtc artifacts"
```

### Task 6: Implement Generated Video Source and Counting Sink

**Files:**
- Create: `client/rtc/CMakeLists.txt`
- Create: `client/rtc/webrtc/CMakeLists.txt`
- Create: `client/rtc/webrtc/src/test_pattern_source.hpp`
- Create: `client/rtc/webrtc/src/test_pattern_source.cpp`
- Create: `client/rtc/webrtc/src/counting_video_sink.hpp`
- Create: `tests/rtc/CMakeLists.txt`
- Create: `tests/rtc/test_pattern_source_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write a failing real-frame test**

Under the WebRTC-enabled build, construct a 640×360 source at 30 fps, attach a
counting sink, start it for 250 ms, stop it, and require:

```cpp
REQUIRE(sink.frame_count() >= 5);
REQUIRE(sink.last_width() == 640);
REQUIRE(sink.last_height() == 360);
REQUIRE(sink.timestamps_increase());
REQUIRE(source.generated_count() >= sink.frame_count());
REQUIRE(source.pending_frame_count() == 0);
```

Also inspect one I420 buffer and require the moving bar regions are not all the
same luma value.

- [ ] **Step 2: Run and observe RED**

```bash
cmake --build --preset build-webrtc-dev \
  --target shareme_test_pattern_source_test
```

Expected: source and target missing.

- [ ] **Step 3: Implement the source and sink**

`TestPatternSource` derives from `webrtc::AdaptedVideoTrackSource`, owns a
libwebrtc task queue and `RepeatingTaskHandle`, and exposes:

```cpp
class TestPatternSource final : public webrtc::AdaptedVideoTrackSource {
public:
  static webrtc::scoped_refptr<TestPatternSource> create(
      webrtc::TaskQueueFactory& task_queue_factory,
      int width,
      int height,
      int frames_per_second);
  void start();
  void stop() noexcept;
  [[nodiscard]] std::uint64_t generated_count() const noexcept;
  [[nodiscard]] std::uint64_t dropped_count() const noexcept;
};
```

Each tick allocates an `I420Buffer`, fills Y/U/V planes with deterministic
moving bars, sets monotonic microsecond and 90 kHz RTP timestamps, calls
`AdaptFrame`, and submits immediately with `OnFrame`. It owns no frame queue.

`CountingVideoSink` implements `VideoSinkInterface<VideoFrame>`, stores only
atomic counters and latest metadata, and marks timestamp regression as a
failure.

- [ ] **Step 4: Verify and commit**

```bash
cmake --build --preset build-webrtc-dev
ctest --preset test-webrtc-dev -R test_pattern --output-on-failure
git add CMakeLists.txt client/rtc tests/CMakeLists.txt tests/rtc
git commit -m "feat(rtc): generate bounded WebRTC test video"
```

### Task 7: Implement Runtime Ownership and In-Process Negotiation

**Files:**
- Create: `client/rtc/webrtc/src/webrtc_runtime.hpp`
- Create: `client/rtc/webrtc/src/webrtc_runtime.cpp`
- Create: `client/rtc/webrtc/src/loopback_signaling.hpp`
- Create: `client/rtc/webrtc/src/loopback_signaling.cpp`
- Create: `tests/rtc/loopback_signaling_test.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

- [ ] **Step 1: Write failing lifecycle tests**

Use two real PeerConnections without media and require:

- runtime starts network, worker, and signaling threads;
- offer/answer and trickled local candidates establish encrypted local ICE;
- candidates arriving before remote description drain in FIFO order;
- a 65th staged candidate produces a visible negotiation failure;
- `stop()` may be called twice;
- destruction completes within five seconds and leaves no callbacks.

- [ ] **Step 2: Run and observe RED**

```bash
cmake --build --preset build-webrtc-dev \
  --target shareme_loopback_signaling_test
```

Expected: runtime and signaling headers missing.

- [ ] **Step 3: Implement runtime and factory creation**

`WebRtcRuntime` creates and starts socket/network, worker, and signaling
threads. Build `PeerConnectionFactoryDependencies`, call `EnableMedia`, install
built-in Opus and VP8 factories, inject the caller-provided ADM, and call
`CreateModularPeerConnectionFactory`.

All creation/destruction runs on the signaling thread. The destructor calls
idempotent `stop()` and joins threads in signaling, worker, network order after
factories have been released.

- [ ] **Step 4: Implement encrypted loopback negotiation**

`LoopbackSignaling` owns two observers and two
`CandidateStager<PendingCandidate, 64>` values. It uses Unified Plan, no ICE
servers, and the standard asynchronous create/set description APIs. Never set
`disable_encryption`.

Post all observer state into the coordinator on the signaling thread. Success
requires both peers' ICE state to be connected/completed and DTLS transport
state to be connected. Preserve the first failure diagnostic.

- [ ] **Step 5: Verify and commit**

```bash
cmake --build --preset build-webrtc-dev
ctest --preset test-webrtc-dev -R loopback_signaling --output-on-failure
git add client/rtc/webrtc tests/rtc
git commit -m "feat(rtc): negotiate encrypted loopback peers"
```

### Task 8: Implement Synthetic and Microphone Audio Modes

**Files:**
- Create: `client/rtc/webrtc/src/audio_device_factory.hpp`
- Create: `client/rtc/webrtc/src/audio_device_factory.cpp`
- Create: `tests/rtc/audio_device_factory_test.cpp`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

- [ ] **Step 1: Write a failing synthetic-audio test**

Create the synthetic ADM, initialize recording, run for 150 ms through a
recording `AudioTransport`, and require at least ten 10 ms, 48 kHz mono frames,
non-zero samples, and no playout output.

Verify microphone mode reports a typed dependency/permission error rather than
falling back to synthetic audio when native ADM initialization fails.

- [ ] **Step 2: Run and observe RED**

```bash
cmake --build --preset build-webrtc-dev \
  --target shareme_audio_device_factory_test
```

Expected: factory header and target missing.

- [ ] **Step 3: Implement explicit audio modes**

For synthetic mode, implement a `ToneCapturer` derived from
`webrtc::TestAudioDeviceModule::Capturer`. It returns 10 ms frames of a
continuous 440 Hz sine wave at 48 kHz mono, keeps phase between calls, and
clamps samples to 16-bit range. Inject it with:

```cpp
webrtc::TestAudioDeviceModule::Create(
    environment,
    std::make_unique<ToneCapturer>(48'000, 1, 440.0, 8'000),
    webrtc::TestAudioDeviceModule::CreateDiscardRenderer(48'000, 1));
```

For microphone mode, call:

```cpp
webrtc::CreateAudioDeviceModule(
    environment, webrtc::AudioDeviceModule::kPlatformDefaultAudio);
```

Initialize only the required recording path. Configure AEC, NS, and AGC on the
microphone `AudioSource`; never apply them to synthetic or future movie audio.

Return an error if the selected mode cannot initialize. Never silently switch
modes. Keep remote playout discarded/disabled in the single-machine probe.

- [ ] **Step 4: Verify and commit**

```bash
cmake --build --preset build-webrtc-dev
ctest --preset test-webrtc-dev -R audio_device --output-on-failure
git add client/rtc/webrtc tests/rtc
git commit -m "feat(rtc): add explicit probe audio devices"
```

### Task 9: Build the End-to-End Probe and CLI

**Files:**
- Create: `client/rtc/webrtc/include/shareme/rtc/webrtc_probe.hpp`
- Create: `client/rtc/webrtc/src/webrtc_probe.cpp`
- Create: `client/tools/CMakeLists.txt`
- Create: `client/tools/webrtc_probe/CMakeLists.txt`
- Create: `client/tools/webrtc_probe/main.cpp`
- Create: `tests/rtc/webrtc_loopback_test.cpp`
- Modify: `CMakeLists.txt`
- Modify: `client/rtc/webrtc/CMakeLists.txt`
- Modify: `tests/rtc/CMakeLists.txt`

- [ ] **Step 1: Write the failing real media integration test**

Run a default three-second synthetic probe and require:

```cpp
const auto result = run_webrtc_probe(ProbeConfig{});
REQUIRE(result.status == ProbeStatus::passed);
REQUIRE(result.connection_time <= 10s);
REQUIRE(result.video_frames_received >= 30);
REQUIRE(result.audio_packets_sent > 0);
REQUIRE(result.audio_packets_received > 0);
REQUIRE(result.audio_bytes_sent > 0);
REQUIRE(result.audio_bytes_received > 0);
REQUIRE(result.selected_candidate_type == "host");
```

Wrap the CTest with a 15-second timeout.

- [ ] **Step 2: Run and observe RED**

```bash
cmake --build --preset build-webrtc-dev \
  --target shareme_webrtc_loopback_test
```

Expected: public adapter and target missing.

- [ ] **Step 3: Implement the probe coordinator**

Expose only:

```cpp
namespace shareme::rtc {
[[nodiscard]] ProbeResult run_webrtc_probe(const ProbeConfig& config);
}
```

Create the selected ADM, runtime, two peers, generated video track
`movie-video`, and audio track `host-voice`. Attach the counting video sink,
disable receiver speaker rendering, negotiate, run media, collect standard
outbound/inbound RTP and candidate-pair stats, then execute ordered shutdown.

Map timeout, permission, negotiation, media, and cleanup failures to the
designed statuses. The first sanitized error wins. Require actual received
frames and RTP counters; state transitions alone never pass.

- [ ] **Step 4: Implement the thin CLI**

Support:

```text
shareme_webrtc_probe
  [--audio synthetic|microphone]
  [--seconds 1..30]
  [--width even-positive]
  [--height even-positive]
  [--fps 1..60]
```

Unknown, repeated, or malformed arguments return exit code 2 and a sanitized
diagnostic. A completed probe prints exactly one JSON object to stdout and
returns zero only for `passed`.

- [ ] **Step 5: Verify and commit**

```bash
cmake --build --preset build-webrtc-dev
ctest --preset test-webrtc-dev -R webrtc_loopback --output-on-failure
./build/webrtc-dev/client/tools/webrtc_probe/shareme_webrtc_probe \
  --audio synthetic --seconds 3
git add CMakeLists.txt client/rtc client/tools tests/rtc
git commit -m "feat(rtc): send WebRTC test video and audio"
```

Expected: JSON reports passed, at least 30 received frames, positive audio RTP
counters, connected host candidates, and process completion within 15 seconds.

### Task 10: Verify, Document, and Publish the WebRTC Slice

**Files:**
- Create: `docs/verification/webrtc-test-media-probe.md`
- Modify: `README.md`
- Modify: `docs/architecture.md`
- Modify: `docs/agent-contracts.md`
- Modify: `docs/superpowers/plans/2026-07-28-webrtc-test-media-probe.md`

- [ ] **Step 1: Run all dependency-off regressions**

```bash
cmake --fresh --preset dev
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure

cmake --fresh --preset media-dev
cmake --build --preset build-media-dev
ctest --preset test-media-dev --output-on-failure

cmake --fresh --preset playback-dev -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev --output-on-failure
```

Expected: portable core, FFmpeg, and Qt playback suites remain green.

- [ ] **Step 2: Run WebRTC synthetic verification**

```bash
cmake --fresh --preset webrtc-dev \
  -DWEBRTC_ROOT=/Users/dio/Library/Caches/ShareMe/webrtc
cmake --build --preset build-webrtc-dev
ctest --preset test-webrtc-dev --output-on-failure
./build/webrtc-dev/client/tools/webrtc_probe/shareme_webrtc_probe \
  --audio synthetic --seconds 3
```

Record the exact revision, platform, compiler, connection time, frame counters,
audio RTP counters, RTT, candidate type, total test count, and exit code.

- [ ] **Step 3: Run real microphone acceptance**

On each available platform:

```bash
./build/webrtc-dev/client/tools/webrtc_probe/shareme_webrtc_probe \
  --audio microphone --seconds 10
```

Grant microphone permission, speak during the interval, and record audio level
and RTP evidence. Do not claim microphone verification for a platform that did
not run.

- [ ] **Step 4: Update contracts and verification status**

Document:

- `client/rtc/webrtc` as the native adapter owner;
- the external dependency and lock/update policy;
- exact automated results;
- exact microphone/manual results;
- current environment blockers, including full-Xcode or Windows-machine
  requirements;
- explicit non-verification of TURN, two-machine, public-network, Qt, H.264,
  and reconnect behavior.

Do not commit generated manifests, WebRTC source, libraries, logs, SDP,
candidates, addresses, device identifiers, or permission records.

- [ ] **Step 5: Run final scope and secret checks**

```bash
git diff --check
git status --short
git diff --stat main...HEAD
git ls-files | rg 'depot_tools|webrtc-checkout|shareme-webrtc-manifest|\\.a$|\\.lib$|\\.log$'
rg -n 'candidate:|v=0|ice-ufrag|ice-pwd' \
  README.md docs client tests scripts deps || true
```

Expected: only intended source, tests, scripts, lock metadata, and docs are
tracked; no dependency output, SDP, ICE secrets, or local paths leak.

- [ ] **Step 6: Commit and push**

```bash
git add README.md docs/architecture.md docs/agent-contracts.md \
  docs/verification/webrtc-test-media-probe.md \
  docs/superpowers/plans/2026-07-28-webrtc-test-media-probe.md
git commit -m "docs: record WebRTC media probe verification"
git push -u origin phase0/webrtc-probe
```

- [ ] **Step 7: Read actual GitHub status**

Read the Core CI conclusion for the pushed head. Report separately:

- dependency-free macOS and Windows CI;
- locally verified WebRTC platform(s);
- manual microphone platform(s);
- blocked or unmeasured environments.

Do not call the Phase 0 WebRTC demonstration complete until one real
libwebrtc-enabled synthetic loopback passes. Do not call Windows or microphone
verified without the corresponding run.
