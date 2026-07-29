# Signaling Foundation Verification

Run the local service with `cd server && go run ./cmd/signaling`. It exposes
`GET /healthz` and `GET /v1/ws`. `SHAREME_SIGNALING_ADDR` overrides the default
`127.0.0.1:8080`; `SHAREME_ICE_SERVERS_JSON` accepts an optional ICE-server array.

Verified by `cd server && go test -race ./... && go vet ./...`, plus the C++ core
CTest suite. This verifies the local signaling service only; it does not verify
native WebRTC clients, TURN, public-network calls, or Windows capture.
