# Qt and FFmpeg Playback Demonstration Design

## Goal

Prove that ShareMe can open a local movie with FFmpeg libraries, decode video
and audio on a worker thread, render the decoded video in a Qt/QML window, play
48 kHz stereo PCM through Qt Multimedia, and recover after pause and seek.

This is a local playback demonstration. It does not send media, use libwebrtc,
encode video, burn subtitles, or claim hardware decoding.

## Considered Approaches

### Direct FFmpeg pipeline with thin Qt adapters — selected

FFmpeg owns demux, decode, timestamp conversion, pixel conversion, and audio
resampling. Portable media values cross into a Qt adapter, which converts video
to `QVideoFrame` and PCM to `QAudioSink`.

This follows the approved architecture, exposes the PTS needed by later
synchronization work, and keeps Qt out of the media backend.

### QMediaPlayer-only demonstration

This would produce a window quickly but would delegate demux, decoding, stream
selection, and timestamps to the platform multimedia backend. It would not
validate the main ShareMe media architecture and is therefore rejected.

### Embedded mpv/libmpv

This offers mature playback but introduces another playback engine and hides
the frame/audio ownership contracts ShareMe must later connect to WebRTC. It is
not justified for the first technical proof.

## Component Boundaries

```text
QML controls and VideoOutput
  -> PlaybackController (Qt facade, UI thread)
     -> PlaybackSession (worker ownership and state)
        -> FfmpegMediaSource (demux, decode, seek)
           -> VideoFrame (RGBA, dimensions, stride, PTS)
           -> AudioFrame (S16 stereo 48 kHz, PTS)
     -> QtVideoOutput (QVideoFrame -> QVideoSink)
     -> QtAudioOutput (PCM -> QAudioSink)
```

`FfmpegMediaSource` contains no Qt types. `PlaybackSession` depends on an
`IMediaSource` contract so pause, seek, shutdown, and stale-generation behavior
can be tested with a deterministic fake source.

## Media Contracts

### Video

The demonstration converts decoded video to packed RGBA:

- width, height, and stride are explicit;
- storage is owned by the frame value;
- PTS is a signed 64-bit millisecond value;
- missing FFmpeg timestamps are synthesized from the previous frame duration;
- rotation metadata is recorded but application is deferred.

RGBA is intentionally a correctness-first boundary for the demonstration. A
later GPU slice replaces it with an opaque GPU frame without changing playback
state or synchronization contracts.

### Audio

Decoded audio is resampled to:

- 48,000 Hz;
- two interleaved channels;
- signed 16-bit samples;
- frame PTS in milliseconds.

Audio is the playback clock. `QAudioSink::processedUSecs()` supplies the local
audio progress after playback starts. Video frames are presented when their PTS
is due and stale video frames are dropped instead of accumulating.

## State and Control

The playback state is:

```text
closed -> opening -> paused -> playing
                     ^   |        |
                     |   +--------+
                     +--- seeking
any state -> failed
any state -> closed
```

- Opening selects the best video stream and optional best audio stream.
- Successful open stops in `paused` at the first decodable position.
- Play starts/resumes the worker and audio output.
- Pause stops presentation without destroying codecs.
- Seek increments a generation, clears queued frames, calls `av_seek_frame`,
  flushes codec buffers, and resumes according to the prior play/pause state.
- Close is idempotent, requests stop, joins the worker, stops Qt audio, and then
  frees FFmpeg resources.

Commands from QML are non-blocking. Results and errors return to the UI thread
through queued Qt signals.

## Queue and Backpressure

- decoded video capacity: 3 frames, drop oldest;
- decoded audio capacity: 500 ms, reject newest and report overload;
- Qt video handoff: one current frame;
- no queue grows in response to pause or a slow renderer.

Every frame includes a seek generation. Consumers discard frames from older
generations.

## Errors

Errors use a stable category and sanitized detail:

- file open failed;
- no video stream;
- decoder unavailable;
- unsupported conversion;
- audio device unavailable;
- decode failed;
- seek failed.

The user-facing message may include the filename but never secret paths from
unrelated resources. FFmpeg error codes remain available to diagnostic logs.

## Dependency and Build Policy

- Qt 6.8 or newer, components Core, Gui, Quick, Qml, and Multimedia;
- FFmpeg libraries `libavformat`, `libavcodec`, `libavutil`, `libswscale`, and
  `libswresample`;
- optional targets are built only when `SHAREME_ENABLE_QT` and
  `SHAREME_ENABLE_FFMPEG` are enabled;
- dependency absence fails configuration when the corresponding option is on;
- the portable core build remains dependency-free and unchanged.

The first implementation is verified on the current macOS ARM64 machine.
Windows build and playback remain environment-bound until run on the user's
Windows machine or a Windows runner with Qt and FFmpeg development packages.

## Verification

Automated tests cover:

- stream metadata and time-base conversion;
- one generated video/audio fixture opened through FFmpeg;
- decoded video dimensions, monotonic PTS, and non-empty RGBA data;
- decoded audio format and monotonic PTS;
- pause, resume, seek-generation, stale-frame rejection, and idempotent close;
- dependency-off portable build remains green.

Manual acceptance uses a non-committed MP4/MKV/MOV sample:

- file opens and the first frame appears;
- video advances while playing and freezes while paused;
- movie audio is audible and stereo;
- seek reaches the requested region and resumes within 500 ms on suitable local
  media;
- closing during playback exits without a hang or crash.

Performance, hardware decoding, long-run drift, and format coverage are measured
in later slices and are not claimed by this demonstration.
