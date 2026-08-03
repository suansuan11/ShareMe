# Receiver Movie Audio Playout Verification

Date: 2026-08-03 (Asia/Shanghai)

## Delivered behavior

- `AudioDeviceMode::playout` opens the platform-default output, initializes
  stereo playout, and does not initialize recording or request microphone
  permission.
- A viewer `MovieAudioPeer` may opt into native playout; hosts reject that
  configuration. Recording stays disabled on the dedicated PeerConnection.
- The Qt RTC demo enables native playout only for its viewer. The headless call
  probe retains its discard renderer so CI and transport smoke do not depend on
  speaker hardware.
- Movie smoke accepts even 16:9 WebRTC-adapted dimensions rather than one
  fixture-specific resolution.

## Supplied-media evidence — macOS arm64

Media tested: episode `01.mkv` from the user-supplied Violet Evergarden 4K
directory. The repository does not store or stage this file.

- `ffprobe`: HEVC Main10 video; FLAC, 48 kHz, stereo audio.
- FFmpeg decoded the first video frame and two seconds of audio successfully.
- Real signaling smoke passed: viewer received 45 frames at WebRTC-adapted
  960x540, 101 valid 48 kHz stereo movie-audio callbacks, zero invalid
  callbacks, and a nonzero peak. Host generated 100 movie-audio chunks;
  bidirectional voice metrics were nonzero.
- A real Qt host/viewer session using the same file stayed connected for about
  ten seconds. Native output initialization produced no sanitized audio-device
  failure, codec collision, or AudioSendStream race diagnostic.

## Automated verification

- Audio-device, movie-peer, signaled-call CLI, and RTC-demo CLI contracts each
  passed ten consecutive runs after implementation.
- The adaptive-dimension contract first failed because the helper was absent,
  then passed for 320x180 and 960x540 while rejecting zero, odd, and 4:3 sizes.
- Full-stage build, CTest, Go race/vet, workflow, and repository validation are
  recorded at the final commit boundary.

## Evidence boundaries

- The current Mac opened the native output path without an error, but tools
  cannot hear or measure speaker sound pressure. Audible confirmation remains
  a manual user acceptance step.
- Windows native speaker selection and output remain environment-dependent.
- No volume/mute control, receiver playout report, hard resync, or TURN/public
  network acceptance is claimed.
- The external libwebrtc cache remained read-only and was not staged.
