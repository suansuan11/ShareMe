# Qt Signaling Client Verification

Build the optional Qt signaling adapter without FFmpeg or WebRTC:

```bash
cmake --preset signaling-dev -DCMAKE_PREFIX_PATH=/opt/homebrew
cmake --build --preset build-signaling-dev
ctest --preset test-signaling-dev
```

Start the local service with `cd server && go run ./cmd/signaling`, then create
and join a room using `build/signaling-dev/client/signaling/shareme_signaling_probe`:

```bash
./shareme_signaling_probe --server ws://127.0.0.1:8080/v1/ws --role host
./shareme_signaling_probe --server ws://127.0.0.1:8080/v1/ws --role viewer --room ROOMID
```

On 2026-07-30, a macOS ARM64 host probe created `M4LJCQ` and a viewer probe
joined it through the local Go service. Default C++ CTest reported 6/6 passing.
This verifies room signaling only; a PeerConnection, media tracks, TURN, and
Windows native media are not yet verified.
