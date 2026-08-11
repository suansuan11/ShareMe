# Windows GUI and Hardware Screen Parity Verification

## Outcome

`partial-windows-hardware-evidence`

The Windows automated product path is verified on the machine described below:
the complete GUI contracts run natively, Desktop Duplication supplies the
screen track, WebRTC selects a hardware Media Foundation H.264 encoder only
after a paired encoder/decoder probe, and the local host/viewer call preserves
video, bidirectional synthetic primary voice, and bounded presentation
recovery. The stage remains partial because two physical Windows devices,
human visual/acoustic checks, a physical 4K display, thermal observation, and
this branch's native macOS regression were unavailable.

No libwebrtc source, checkout, build flow, or locked revision was changed or
downloaded for this work. The existing repository-external build for locked
revision `5ad58d70eea10785fab05ba4150e2fe22ecc7f97` was consumed read-only.

## Test Environment

- Windows 11 Pro AMD64, version 10.0.26200, build 26200.
- AMD Ryzen 5 9600X, 12 logical processors, 15.6 GiB RAM.
- NVIDIA GeForce RTX 5060 Ti; the machine also exposes a virtual display
  adapter.
- Primary display 2560x1440 at 160 Hz, 100% scale (96 DPI).
- Desktop system on AC power, Windows balanced power plan.
- Qt 6.11.1 and MSVC 19.51.
- Final measured demo SHA-256:
  `4d76fbe63e956d1d411b9f07fa84bc26c588a6a4e8633011bc86cca49b849314`.
- Final motion-fixture SHA-256:
  `3ffbda3c35d81c59f150d6b0264b5dee1d728794c407ff2593539e98d86880c0`.

Machine identifiers, usernames, absolute paths, room IDs, raw command lines,
and unsanitized child-process output are excluded from committed and generated
evidence.

## Native Build and Regression

- `call-dev` built with the locked WebRTC archive and passed 55/55 CTest tests.
- `movie-call-dev` built with the installed Qt/FFmpeg dependencies and passed
  80/80 CTest tests.
- `signaled_peer` passed 20 consecutive executions.
- The Go signaling server passed `go test ./...` and `go vet ./...`.
- Workflow unit tests passed 8/8; the ShareMe skill validator passed.
- `go test -race ./...` is environment-dependent: the current Windows Go
  environment reports that `-race` requires cgo, and no compatible cgo
  toolchain was available.
- Native macOS regression is environment-dependent. WIN32 guards and portable
  tests protect the shared selector boundary, but Windows results do not prove
  VideoToolbox or macOS GUI behavior.

## GUI Acceptance

`windows-gui-acceptance-v1` passed all six automated state/action probes:
home, create, join, host call, viewer call, and host call actions. The native
motion fixture emitted 175 frames during its bounded three-second run. The
idle process measurement contained 12 samples, 0.303% mean CPU, 1.042% maximum
CPU, and 33,892 KiB maximum RSS.

Only 100% scaling was available. The 125%, 150%, and 200% DPI modes are
environment-dependent. Human checks for the eight visible GUI surfaces,
minimum size, keyboard focus, audio controls, leave, and return-home are
`not-run`; process survival and offscreen QML probes are not presented as
visual acceptance.

## Hardware Codec Boundary

The Windows codec checkpoint covers bounded Annex-B/AVCC handling, H.264
parameter-set and keyframe classification, Media Foundation encoder/decoder
initialization, encode/decode callbacks, timestamps, geometry, rate changes,
keyframe requests, release, and fallback classification. Windows exposes H.264
only when both hardware encoder probing and the compatible decoder path
succeed. An explicit diagnostic `--screen-encoder software` mode is restricted
to the standard profile and reports VP8 plus
`fallback:explicit-software`; the GUI remains `auto`.

## Standard Performance Gate

The accepted comparison uses three sequential 180-second software baseline
runs and three sequential 180-second hardware candidate runs from the same
demo SHA. Each run contains 121 samples in the frozen 30-150 second measurement
window and uses exact 1920x1080 geometry.

| Metric | VP8 software median | MF H.264 median | Gate |
| --- | ---: | ---: | --- |
| Host CPU mean | 6.5545% | 4.1118% | 37.2675% reduction; pass (at least 30%) |
| Host CPU P95 | 9.5147% | 5.7292% | pass; no regression |
| Host RSS P95 | baseline | candidate | -14.9605% growth; pass (at most 15%) |
| Hardware submitted cadence | n/a | 99.9868% | pass (at least 95%) |

All six runs passed geometry, media continuity, bidirectional synthetic primary
voice, bounded recovery, and quality classification. Earlier `mf-run-*`
attempts are retained only as diagnostic evidence: an older motion fixture was
occluded and stopped changing after roughly 36 seconds. They are excluded from
the frozen `*-v2-*` comparison; the always-on-top fixture and lifecycle guard
were rerun for every accepted sample.

## Quality Profile Gate

The 120-second quality run passed at exact 2560x1440 with H.264,
`MediaFoundation`, and `hardware_encoder_status=active`. Submitted cadence was
99.9611%. The 30-119 second measurement window contained 90 samples: host CPU
mean was 6.1068%, host CPU P95 was 7.5521%, and host RSS P95 was 257,810,432
bytes. Viewer CPU mean was 4.8061%, viewer CPU P95 was 12.8906%, and viewer RSS
P95 was 216,276,992 bytes. Video, voice, and one bounded presentation recovery
passed.

Cinema 3840x2160@30 was not run because the available physical display is
2560x1440. This is environment-dependent and no 4K claim is made.

## Human and Physical Evidence

The following items are `not-run` or `environment-dependent` because only one
local Windows machine was available:

- two-device create/join and matching binary identity;
- human inspection of color bars, thin lines, gradients, text, motion, cursor,
  aspect ratio, and green/black-frame absence;
- audible bidirectional voice, mute/speaker controls, echo, and continuity;
- minimize/restore, display sleep or lock recovery on two devices;
- leave/rejoin and remote host-termination behavior;
- 20-minute standard and 10-minute quality/cinema human sessions;
- controlled subjective heat and fan comparison.

Cursor composition and display selection remain product gaps outside this
stage. System audio, HDR claims, D3D11 zero-copy, remote input, TURN/public
deployment, Movie Stage 2B, hard resync, installer/signing, auto-update, and
4K60 remain out of scope.

## Reproduction

From an x64 Visual Studio developer shell, reuse the existing locked WebRTC
directory and substitute local dependency paths:

```powershell
cmake --fresh --preset call-dev `
  -DWEBRTC_ROOT=<existing-shareme-webrtc-root> `
  -DCMAKE_PREFIX_PATH=<qt-msvc-prefix> `
  -DCMAKE_LINKER=<locked-webrtc-lld-link>
cmake --build --preset build-call-dev --config Release
ctest --test-dir build/call-dev -C Release --output-on-failure

$env:PATH = '<qt-bin>;' + $env:PATH
Start-Process -WindowStyle Hidden -FilePath go -ArgumentList 'run','./cmd/signaling' `
  -WorkingDirectory server
build/call-dev/shareme_rtc_demo.exe
```

The no-argument executable opens the complete GUI and defaults screen encoder
selection to `auto`. The automated acceptance contracts can be rerun with:

```powershell
python scripts/run_windows_gui_acceptance.py `
  --demo build/call-dev/shareme_rtc_demo.exe `
  --fixture build/call-dev/shareme_screen_motion_fixture.exe `
  --artifact out/windows-gui-hardware-parity/gui.json

python scripts/run_windows_screen_acceptance.py `
  --mode hardware --profile standard --duration-seconds 180 --port 18301 `
  --artifact out/windows-gui-hardware-parity/performance/mf-run.jsonl
```

Generated JSON/JSONL, screenshots, logs, build trees, and machine-local settings
remain ignored evidence and must not be committed.
