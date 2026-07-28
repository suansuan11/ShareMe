# Qt and FFmpeg Playback Demonstration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a local Qt/QML demonstration that decodes movie video and audio through FFmpeg libraries, presents frames by PTS, supports play/pause/seek, and preserves a dependency-free portable core build.

**Architecture:** A Qt-free `FfmpegMediaSource` returns owned RGBA video and 48 kHz stereo S16 audio values. A worker-owned playback session emits generation-tagged frames to a thin Qt facade, which writes PCM to `QAudioSink` and video frames to the QML `VideoOutput` sink.

**Tech Stack:** C++20, CMake 3.25+, FFmpeg libraries, Qt 6.8+ Core/Gui/Qml/Quick/QuickControls2/Multimedia, CTest

---

### Task 1: Add Optional FFmpeg and Qt Build Discovery

**Files:**
- Create: `cmake/FindFFmpeg.cmake`
- Create: `client/media/CMakeLists.txt`
- Create: `client/media/playback/CMakeLists.txt`
- Create: `client/app/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `CMakePresets.json`

- [x] **Step 1: Add FFmpeg imported targets**

Use `PkgConfig::AVFORMAT`, `PkgConfig::AVCODEC`, `PkgConfig::AVUTIL`,
`PkgConfig::SWSCALE`, and `PkgConfig::SWRESAMPLE` to expose:

```cmake
FFmpeg::avformat
FFmpeg::avcodec
FFmpeg::avutil
FFmpeg::swscale
FFmpeg::swresample
```

Set `FFmpeg_FOUND` only when all five libraries are present.

- [x] **Step 2: Gate optional targets**

When `SHAREME_ENABLE_FFMPEG=ON`, require FFmpeg and add `client/media`.
When `SHAREME_ENABLE_QT=ON`, require FFmpeg as well as Qt 6.8 components Core,
Gui, Qml, Quick, QuickControls2, and Multimedia, then add `client/app`.

- [x] **Step 3: Add local media and playback presets**

Create `media-dev`, `build-media-dev`, and `test-media-dev` presets that enable
only FFmpeg. Create `playback-dev`, `build-playback-dev`, and
`test-playback-dev` presets that enable FFmpeg and Qt. Developers supply
`Qt6_ROOT` through `CMakeUserPresets.json` or the environment; no machine path
is committed.

- [x] **Step 4: Verify dependency-off behavior**

Run:

```bash
cmake --fresh --preset dev
cmake --build --preset build-dev
ctest --preset test-dev
```

Expected: the existing portable build passes without locating FFmpeg or Qt.

- [x] **Step 5: Verify dependency-on failure before Qt installation**

Run:

```bash
cmake --fresh --preset playback-dev
```

Expected before Qt installation: configuration fails with a missing Qt package
message, proving there is no silent fallback.

- [x] **Step 6: Commit build discovery**

```bash
git add CMakeLists.txt CMakePresets.json cmake/FindFFmpeg.cmake client/media/CMakeLists.txt client/media/playback/CMakeLists.txt client/app/CMakeLists.txt
git commit -m "build: add optional Qt and FFmpeg playback targets"
```

### Task 2: Define Media Values and Timestamp Conversion with TDD

**Files:**
- Create: `client/media/playback/include/shareme/media/media_frame.hpp`
- Create: `client/media/playback/include/shareme/media/media_time.hpp`
- Create: `client/media/playback/src/media_time.cpp`
- Create: `tests/media/CMakeLists.txt`
- Create: `tests/media/media_time_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1: Write the failing timestamp tests**

Verify:

```cpp
REQUIRE(to_milliseconds(90'000, Rational{1, 90'000}) == 1'000);
REQUIRE(to_milliseconds(45'000, Rational{1, 90'000}) == 500);
REQUIRE(to_milliseconds(-45'000, Rational{1, 90'000}) == -500);
REQUIRE_FALSE(to_milliseconds(kNoTimestamp, Rational{1, 90'000}).has_value());
```

Also verify invalid zero denominators are rejected.

- [x] **Step 2: Run and observe RED**

```bash
cmake --build --preset build-media-dev --target shareme_media_time_test
```

Expected: compilation fails because `media_time.hpp` is absent.

- [x] **Step 3: Implement portable media values**

Define:

```cpp
struct VideoFrame {
  std::vector<std::byte> rgba;
  int width;
  int height;
  int stride;
  std::int64_t pts_ms;
  std::uint64_t generation;
};

struct AudioFrame {
  std::vector<std::int16_t> interleaved_samples;
  int sample_rate;
  int channels;
  std::int64_t pts_ms;
  std::uint64_t generation;
};
```

Use checked integer rescaling for `Rational`; no FFmpeg type crosses this
header.

- [x] **Step 4: Verify GREEN and commit**

```bash
cmake --build --preset build-media-dev --target shareme_media_time_test
ctest --preset test-media-dev -R media_time
git add client/media/playback tests
git commit -m "feat(media): add portable frame and timestamp contracts"
```

### Task 3: Implement FFmpeg Open and Decode with an Integration Test

**Files:**
- Create: `client/media/playback/include/shareme/media/media_source.hpp`
- Create: `client/media/playback/include/shareme/media/ffmpeg_media_source.hpp`
- Create: `client/media/playback/src/ffmpeg_media_source.cpp`
- Create: `tests/media/ffmpeg_media_source_test.cpp`
- Modify: `client/media/playback/CMakeLists.txt`
- Modify: `tests/media/CMakeLists.txt`

- [x] **Step 1: Register a generated fixture**

Use the local `ffmpeg` executable from CTest to generate a one-second MP4 under
the build directory:

```bash
ffmpeg -y \
  -f lavfi -i testsrc2=size=160x90:rate=30 \
  -f lavfi -i sine=frequency=440:sample_rate=48000 \
  -t 1 -c:v mpeg4 -pix_fmt yuv420p -c:a aac generated-playback.mp4
```

Mark the generation test as a CTest fixture setup. Do not commit the MP4.

- [x] **Step 2: Write the failing integration test**

Open the fixture and require:

- duration between 900 and 1,100 ms;
- video size 160x90;
- video and audio streams detected;
- at least one non-empty RGBA frame;
- at least one 48 kHz, two-channel audio frame;
- nondecreasing PTS within each stream.

- [x] **Step 3: Run and observe RED**

```bash
cmake --build --preset build-media-dev --target shareme_ffmpeg_media_source_test
```

Expected: compilation fails because `ffmpeg_media_source.hpp` is absent.

- [x] **Step 4: Implement open and decode**

`FfmpegMediaSource` owns `AVFormatContext`, video/audio `AVCodecContext`,
`SwsContext`, `SwrContext`, `AVPacket`, and `AVFrame` through a private
implementation. Use:

- `avformat_open_input` and `avformat_find_stream_info`;
- `av_find_best_stream`;
- `avcodec_send_packet`/`avcodec_receive_frame`;
- `sws_scale` into RGBA;
- `swr_alloc_set_opts2` and `swr_convert` into 48 kHz stereo S16.

The public result is:

```cpp
using MediaEvent = std::variant<VideoFrame, AudioFrame, EndOfStream>;

class IMediaSource {
public:
  virtual ~IMediaSource() = default;
  virtual MediaInfo open(const std::filesystem::path& path) = 0;
  virtual MediaEvent read_next(std::uint64_t generation) = 0;
  virtual void seek(std::int64_t target_ms) = 0;
  virtual void close() noexcept = 0;
};
```

- [x] **Step 5: Verify decode GREEN**

```bash
cmake --build --preset build-media-dev --target shareme_ffmpeg_media_source_test
ctest --preset test-media-dev -R "generate_media_fixture|ffmpeg_media_source"
```

Expected: fixture generation and decode integration pass.

- [x] **Step 6: Add failing seek assertions**

Seek to 600 ms, decode until the first video frame, and require its PTS to be at
or after the nearest keyframe and no later than 700 ms. Require all returned
frames to carry the new generation.

- [x] **Step 7: Implement seek and verify**

Use `av_seek_frame`, clear pending events, call `avcodec_flush_buffers` for
both codecs, reset EOF state, and reinitialize resampler delay.

Run the full media suite, then commit:

```bash
ctest --preset test-media-dev -R "media_time|generate_media_fixture|ffmpeg_media_source"
git add client/media/playback tests/media
git commit -m "feat(media): decode local movies with FFmpeg"
```

### Task 4: Implement Playback State and Generation Handling with TDD

**Files:**
- Create: `client/media/playback/include/shareme/media/playback_session.hpp`
- Create: `client/media/playback/src/playback_session.cpp`
- Create: `tests/media/playback_session_test.cpp`
- Modify: `client/media/playback/CMakeLists.txt`
- Modify: `tests/media/CMakeLists.txt`

- [x] **Step 1: Write a deterministic fake source**

The fake records open/read/seek/close calls and yields caller-supplied
generation-tagged frames.

- [x] **Step 2: Write failing state tests**

Verify:

- successful open enters paused;
- play and pause are idempotent;
- seek increments generation and clears queued frames;
- frames from an older generation are discarded;
- video queue capacity is three and drops oldest;
- close requests stop, joins the worker, and calls source close once.

- [x] **Step 3: Run and observe RED**

```bash
cmake --build --preset build-media-dev --target shareme_playback_session_test
```

Expected: compilation fails because `playback_session.hpp` is absent.

- [x] **Step 4: Implement the minimum session**

Own one `std::jthread`, one source, bounded video/audio queues, atomic stop and
state, and a monotonically increasing generation. Never invoke Qt from the
worker.

- [x] **Step 5: Verify and commit**

```bash
cmake --build --preset build-media-dev
ctest --preset test-media-dev
git add client/media/playback tests/media
git commit -m "feat(media): add local playback session"
```

### Task 5: Build the Qt/QML Playback Demonstration

**Files:**
- Create: `client/app/src/main.cpp`
- Create: `client/app/src/playback_controller.hpp`
- Create: `client/app/src/playback_controller.cpp`
- Create: `client/app/qml/Main.qml`
- Create: `client/app/qml/PlaybackControls.qml`
- Modify: `client/app/CMakeLists.txt`

- [x] **Step 1: Install and verify Qt locally**

```bash
brew install qtbase qtdeclarative qtmultimedia
cmake --fresh --preset playback-dev -DCMAKE_PREFIX_PATH=/opt/homebrew
```

Expected: Qt 6.8 or newer and all required components are found.

- [x] **Step 2: Add the Qt executable**

Use `qt_add_executable` and `qt_add_qml_module`. Link `ShareMe::Playback`,
Qt6::Core, Qt6::Gui, Qt6::Qml, Qt6::Quick, Qt6::QuickControls2, and
Qt6::Multimedia.

- [x] **Step 3: Implement the controller**

Expose:

```cpp
Q_PROPERTY(QString state READ state NOTIFY stateChanged)
Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY durationChanged)
Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

Q_INVOKABLE void open(const QUrl& url);
Q_INVOKABLE void play();
Q_INVOKABLE void pause();
Q_INVOKABLE void seek(qint64 target_ms);
Q_INVOKABLE void setVideoSink(QVideoSink* sink);
```

Convert RGBA storage to a detached `QImage`, then to `QVideoFrame`, set its
start/end timestamps, and pass it to `QVideoSink::setVideoFrame`. Configure
`QAudioSink` for 48 kHz, stereo, Int16 and write only available bytes to its
push-mode `QIODevice`.

- [x] **Step 4: Implement focused QML**

Provide a file dialog, video output, open/play/pause buttons, seek slider,
current/duration labels, and an inline error banner. Disable invalid actions
from controller state. Do not add room or network UI.

- [x] **Step 5: Build and launch**

```bash
cmake --build --preset build-playback-dev --target shareme_playback_demo
./build/playback-dev/client/app/shareme_playback_demo
```

Expected: the window opens and remains responsive before a file is selected.

- [x] **Step 6: Commit the application**

```bash
git add client/app
git commit -m "feat(app): add Qt local playback demonstration"
```

### Task 6: Verify and Publish the Playback Slice

**Files:**
- Modify: `README.md`
- Modify: `.github/workflows/core-ci.yml` only if portable CI needs path coverage.
- Create: `docs/verification/qt-ffmpeg-playback.md`

- [x] **Step 1: Document exact local commands and status**

Record dependency versions, generated-fixture results, manual media used without
committing its path or contents, and separate verified from environment-bound
Windows results.

- [x] **Step 2: Run portable regression**

```bash
cmake --fresh --preset dev
cmake --build --preset build-dev
ctest --preset test-dev
```

Expected: existing portable tests pass with optional dependencies disabled.

- [x] **Step 3: Run playback verification**

```bash
cmake --fresh --preset playback-dev -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-playback-dev
ctest --preset test-playback-dev
```

Expected: all portable and media tests pass.

- [ ] **Step 4: Perform manual acceptance**

Open a non-committed local movie and verify open, play, pause, audible stereo
audio, seek recovery, and close during playback. Record observed failures
instead of weakening acceptance criteria.

- [ ] **Step 5: Commit and push**

```bash
git add README.md docs/verification/qt-ffmpeg-playback.md .github/workflows/core-ci.yml
git commit -m "docs: record Qt FFmpeg playback verification"
git push -u origin phase0/qt-ffmpeg-playback
```

- [ ] **Step 6: Check GitHub status**

Read the actual workflow conclusion for the pushed commit. Do not report the
playback slice as Windows-verified until it has built and played on Windows.
