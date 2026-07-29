# Stage 1 Signaling Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and test a local Go WebSocket signaling service that provides one-host/one-viewer rooms, temporary participant credentials, configurable ICE servers, and opaque WebRTC negotiation relaying.

**Architecture:** `server/` is an independent Go module. The protocol package validates the shared v1 JSON envelope; the room manager owns mutable room and token state; the WebSocket handler maps socket lifecycle to manager calls. The service relays bounded SDP/ICE payloads but does not inspect their content.

**Tech Stack:** Go 1.26, `nhooyr.io/websocket`, `net/http`, `httptest`, C++20 core regression tests via CMake/CTest.

---

## File structure

- Create: `server/go.mod`, `server/go.sum` — independent module and WebSocket dependency.
- Create: `server/internal/protocol/message.go` and `_test.go` — v1 envelope and payload validation.
- Create: `server/internal/auth/token.go` and `_test.go` — random, hashed, expiring tokens.
- Create: `server/internal/ice/config.go` and `_test.go` — startup ICE JSON parser.
- Create: `server/internal/room/manager.go` and `_test.go` — synchronized room/token/connection state.
- Create: `server/internal/ws/handler.go` and `_test.go` — HTTP/WebSocket boundary and integration coverage.
- Create: `server/cmd/signaling/main.go` — configuration, health endpoint, graceful shutdown.
- Modify: `docs/protocols.md` — exact bootstrap, reconnect, success and error envelopes.
- Modify: `README.md` and `docs/verification/signaling-foundation.md` — local run and verified boundary.

### Task 1: Initialize module and validate protocol envelopes

**Files:**
- Create: `server/go.mod`
- Create: `server/internal/protocol/message.go`
- Create: `server/internal/protocol/message_test.go`

- [ ] **Step 1: Write failing protocol tests.**

```go
func TestDecodeAcceptsCreateRoom(t *testing.T) {
  message, err := Decode([]byte(`{"version":1,"type":"create-room","sequence":1,"payload":{"role":"host"}}`))
  if err != nil || message.Type != "create-room" { t.Fatalf("Decode() = %#v, %v", message, err) }
}
func TestDecodeRejectsInvalidEnvelope(t *testing.T) {
  for _, raw := range [][]byte{[]byte(`{"version":2,"type":"create-room","sequence":1,"payload":{}}`), []byte(`{"version":1,"type":"bad_type","sequence":0,"payload":[]}`)} {
    if _, err := Decode(raw); err == nil { t.Fatalf("Decode(%s) succeeded", raw) }
  }
}
```

- [ ] **Step 2: Run test to prove the initial failure.**

Run: `cd server && go test ./internal/protocol`

Expected: FAIL before `message.go` exists.

- [ ] **Step 3: Implement the smallest protocol surface.**

Create a Go module named `github.com/suansuan11/ShareMe/server` with Go 1.26 and `nhooyr.io/websocket v1.8.17`. Define `Message { Version int; Type, RoomID string; Sequence uint64; Payload json.RawMessage }`, `ErrorPayload { Code, Message string; Retryable bool; RelatedSequence *uint64 }`, `Decode([]byte) (Message, error)`, `Encode(Message)`, and `NewError`. Reject input above 64 KiB, unsupported version, non-positive sequence, invalid lower-case kebab-case type, malformed room ID, absent/non-object payload, and unknown JSON top-level fields.

- [ ] **Step 4: Format and verify the package.**

Run: `cd server && gofmt -w internal/protocol && go test ./internal/protocol`

Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add server/go.mod server/go.sum server/internal/protocol
git commit -m "feat: add signaling protocol validation"
```

### Task 2: Implement credentials, ICE parsing, and room state

**Files:**
- Create: `server/internal/auth/token.go`, `server/internal/auth/token_test.go`
- Create: `server/internal/ice/config.go`, `server/internal/ice/config_test.go`
- Create: `server/internal/room/manager.go`, `server/internal/room/manager_test.go`

- [ ] **Step 1: Write failing state tests.**

```go
func TestIssueAndLookupExpires(t *testing.T) {
  now := time.Unix(100, 0); store := auth.NewStore(func() time.Time { return now })
  raw, record, err := store.Issue("ABCDEF", room.Host, time.Minute)
  if err != nil || record.Role != room.Host { t.Fatal(err) }
  now = now.Add(time.Minute)
  if _, ok := store.Lookup(raw); ok { t.Fatal("expired token resolved") }
}
func TestSecondViewerIsRejected(t *testing.T) {
  manager := newTestManager(t); created, _ := manager.CreateHost()
  if _, err := manager.JoinViewer(created.RoomID); err != nil { t.Fatal(err) }
  if _, err := manager.JoinViewer(created.RoomID); !errors.Is(err, room.ErrViewerOccupied) { t.Fatalf("err = %v", err) }
}
```

- [ ] **Step 2: Run the three packages to prove failure.**

Run: `cd server && go test ./internal/auth ./internal/ice ./internal/room`

Expected: FAIL before implementations exist.

- [ ] **Step 3: Implement explicit state contracts.**

`auth.Store` issues 32 random bytes with `crypto/rand`, URL-safe base64 token strings, and SHA-256 digest map keys; it stores room ID, role as a plain string, expiry, and never stores plaintext. Define `room.Role` as a string alias so `room.Host` and `room.Viewer` can be passed to this API without an import cycle. `ice.Load(raw string)` returns an empty list for an unset variable and accepts only objects whose `urls` is a non-empty string array. `room.Manager` is mutex protected and exposes `CreateHost`, `JoinViewer`, `Bind`, `Disconnect`, `Cleanup`, and `Close`. It creates six-character random base32 IDs, binds one connection per role, applies a 30-second grace deadline, and makes host-grace expiry close the room while viewer-grace expiry frees only the viewer slot. Export typed errors: `ErrRoomNotFound`, `ErrViewerOccupied`, `ErrRoleMismatch`, `ErrInvalidToken`, `ErrPeerUnavailable`.

- [ ] **Step 4: Verify state packages and race safety.**

Run: `cd server && gofmt -w internal/auth internal/ice internal/room && go test -race ./internal/auth ./internal/ice ./internal/room`

Expected: PASS without a race report.

- [ ] **Step 5: Commit.**

```bash
git add server/internal/auth server/internal/ice server/internal/room
git commit -m "feat: add signaling room state"
```

### Task 3: Add WebSocket handler and end-to-end tests

**Files:**
- Create: `server/internal/ws/handler.go`
- Create: `server/internal/ws/handler_test.go`
- Create: `server/cmd/signaling/main.go`

- [ ] **Step 1: Write failing WebSocket integration tests with `httptest`.**

```go
func TestCreateJoinAndRelay(t *testing.T) {
  server := httptest.NewServer(ws.NewHandler(testManager(t), nil)); defer server.Close()
  host := dial(t, server.URL, ""); host.Send(createHost(1)); created := host.ReceiveRoom(t, "room-created")
  viewer := dial(t, server.URL, ""); viewer.Send(joinViewer(1, created.RoomID)); viewer.ReceiveRoom(t, "room-joined")
  host.ExpectType(t, "participant-joined")
  host.Send(description(2, created.RoomID, "opaque-sdp")); viewer.ExpectPayload(t, "session-description", "opaque-sdp")
}
```

Cover `/healthz`, malformed and unauthorized relay errors, duplicate viewer rejection, no-peer retryable error, bearer-token reconnect, and opaque SDP plus ICE forwarding. Decode every received message through `protocol.Decode` and compare opaque payload fields without parsing SDP or candidates.

- [ ] **Step 2: Run focused integration tests to prove failure.**

Run: `cd server && go test ./internal/ws -run TestCreateJoinAndRelay -v`

Expected: FAIL before handler implementation exists.

- [ ] **Step 3: Implement the HTTP boundary.**

`ws.NewHandler(manager, iceServers)` serves `GET /healthz` with exactly `{"status":"ok"}\n`, upgrades `/v1/ws`, limits frames to 64 KiB, and runs one read/write loop per connection. Before binding, accept only `create-room` and `join-room`; a bearer token binds a reconnect at upgrade. On success, send `room-created` or `room-joined` with `{roomId, token, tokenExpiresAtMs, iceServers}`. Relay valid negotiation messages unchanged to the active opposite role; lifecycle events become `participant-joined`, `participant-left`, and `room-closed`. `main.go` loads `SHAREME_ICE_SERVERS_JSON`, listens on `SHAREME_SIGNALING_ADDR` (default `127.0.0.1:8080`), and uses `signal.NotifyContext` plus `http.Server.Shutdown`.

- [ ] **Step 4: Run Go verification.**

Run: `cd server && gofmt -w cmd internal && go test -race ./... && go vet ./...`

Expected: PASS.

- [ ] **Step 5: Commit.**

```bash
git add server
git commit -m "feat: add local signaling service"
```

### Task 4: Align documents and verify the complete slice

**Files:**
- Modify: `docs/protocols.md`
- Modify: `README.md`
- Create: `docs/verification/signaling-foundation.md`

- [ ] **Step 1: Document exact wire behavior.**

Add the unauthenticated bootstrap rule, `Authorization: Bearer` reconnect rule, 10-minute token lifetime, 30-second grace period, `room-joined` response, success payload fields, and error payload schema. Preserve the existing shared v1 envelope and state that relay payloads remain opaque.

- [ ] **Step 2: Add run and evidence documentation.**

Document `cd server && go run ./cmd/signaling`, both endpoints, optional `SHAREME_ICE_SERVERS_JSON`, and exact verification commands. State that this verifies service behavior only—not native desktop WebRTC, TURN, or Windows capture.

- [ ] **Step 3: Run full acceptance from the worktree.**

Run: `cd server && go test -race ./... && go vet ./...`

Run: `cmake --preset dev && cmake --build --preset dev && ctest --test-dir build/dev --output-on-failure`

Expected: both commands exit 0 and CTest reports zero failures.

- [ ] **Step 4: Confirm only intended source and documentation are staged.**

Run: `git status --short && git diff --check && git ls-files | rg '(^|/)(build|bin|tmp|\\.DS_Store)(/|$)'`

Expected: no generated Go, CMake, or operating-system artifacts are added.

- [ ] **Step 5: Commit and push.**

```bash
git add README.md docs/protocols.md docs/verification/signaling-foundation.md
git commit -m "docs: record signaling service verification"
git push
```

## Self-review

| Design requirement | Plan task |
| --- | --- |
| Local Go process, health and WebSocket endpoint | Task 3 |
| One host, one viewer, cryptographic room IDs | Task 2 |
| Temporary hashed tokens and reconnect grace | Task 2 and Task 3 |
| Configured ICE list and empty P2P default | Task 2 and Task 3 |
| Opaque bounded SDP/ICE relay | Task 1 and Task 3 |
| Protocol contract, errors, no secret logging | Task 1 and Task 4 |
| Unit, integration, race, vet and C++ regression tests | Tasks 1–4 |
| No TURN, persistence, desktop client, or media claim | Task 4 |

The plan has no placeholder tasks: every implementation step names its files, contract, and verification command. `room.Role`, `room.Manager`, and the protocol `Message` types are introduced before later tasks reference them.
