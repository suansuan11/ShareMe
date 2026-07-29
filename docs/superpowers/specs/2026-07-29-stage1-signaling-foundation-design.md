# Stage 1 Signaling Foundation Design

## Goal

Deliver a locally runnable Go signaling service that enforces ShareMe's
one-host/one-viewer room contract and relays opaque WebRTC negotiation
messages. It establishes a tested network boundary for the following desktop
client WebSocket and two-client call slice.

## Scope

This slice creates an in-memory Go service with `/healthz` and `/v1/ws`, room
creation and joining, short-lived participant tokens, ICE-server configuration,
and SDP/ICE relaying. It also makes the bootstrap and reconnect authentication
rules explicit in `docs/protocols.md`.

It does not create a Qt WebSocket client, bind a real PeerConnection to the
service, deploy coturn, persist rooms, relay media, or implement movie tracks,
reconnect UI, bitrate adaptation, or Windows capture.

## Architecture

```text
desktop client A                         desktop client B
       |                                        |
       +------- WebSocket JSON envelope --------+
                         |
                   Go signaling service
          +--------------+--------------+
          | room manager | token store  | ICE config |
          +--------------+--------------+------------+
                         |
              relay only SDP and ICE payloads
```

The service does not parse SDP or ICE addresses. It validates the shared
protocol envelope, confirms sender role and room membership, then relays a
bounded payload only to the opposite participant. Media remains peer-to-peer
and encrypted by WebRTC.

## Service layout

```text
server/
  cmd/signaling/main.go        process startup and configuration
  internal/auth/token.go       random token issue, hash, expiry, lookup
  internal/ice/config.go       immutable startup ICE configuration
  internal/protocol/message.go envelope decoding and typed payload validation
  internal/room/manager.go     room lifecycle and participant ownership
  internal/ws/handler.go       WebSocket connection lifecycle and relaying
```

The Go module uses `nhooyr.io/websocket`; its resolved version is recorded in
`server/go.mod` and `server/go.sum`. The server uses the Go standard library
for HTTP, cryptographic randomness, SHA-256 token lookup keys, and time.

## Connection and authentication model

`/v1/ws` accepts an unauthenticated bootstrap connection only until it sends a
valid `create-room` or `join-room` message. The successful response binds that
connection to the newly issued participant token. The server then requires the
bound role and room for all further messages.

A reconnecting client supplies `Authorization: Bearer <participant-token>` in
the WebSocket handshake. The token is 32 random bytes encoded with URL-safe
base64, stored only as a SHA-256 digest, and expires 10 minutes after issue.
The token value is never logged or returned after its initial room response.

Host and viewer each have one active connection. A disconnected participant
has a 30-second reconnect grace period. During that time the opposite peer
receives `participant-left`; a valid reconnect restores the same role and
emits `participant-joined`. Grace expiry removes a viewer; host expiry closes
the entire room and invalidates both tokens.

## Room and message rules

- Room IDs are six uppercase base32 characters generated from cryptographic
  randomness; collision retries are bounded and failure is a server error.
- A room holds exactly one host and one viewer. A second viewer is rejected
  without changing the existing participant.
- `create-room` requires `payload.role == "host"`; `join-room` requires
  `payload.role == "viewer"` and an existing room ID.
- Successful room responses include the room ID, participant token, token
  expiry in milliseconds, and the configured ICE server list.
- `session-description`, `ice-candidate`, and `restart-ice` require a bound
  participant and are relayed only to the opposite active participant.
- The existing version-1 envelope limits remain authoritative: 64 KiB maximum
  message size, valid UTF-8, positive JSON-safe sequence, and lower-case
  kebab-case message type.
- Unknown required fields, role violations, stale room IDs, invalid tokens,
  a missing opposite participant, and malformed payloads return a typed error
  envelope. The connection stays open for malformed application messages and
  closes only for WebSocket transport failure or repeated frame-size violation.

## ICE configuration

At process startup, `SHAREME_ICE_SERVERS_JSON` may contain a JSON array of
ICE-server objects. The service parses it once, validates non-empty URL lists,
and exposes the immutable list in successful room responses. An unset variable
uses an empty list, which supports local host-candidate P2P testing. TURN
credential generation and coturn deployment are deliberately deferred to the
next slice.

## Lifecycle and observability

`/healthz` returns HTTP 200 and `{"status":"ok"}` after configuration is
valid. The process logs only event categories, room IDs, roles, and error
codes. It never logs participant tokens, authorization headers, SDP, ICE
candidates, addresses, device identifiers, or ICE credentials.

Rooms and expired reconnect grace entries are cleaned by one manager-owned
timer loop. Service shutdown closes listener connections, stops that loop, and
invalidates in-memory room state.

## Testing and acceptance

Go unit tests cover token expiry, room capacity, role enforcement, reconnect
grace, cleanup, and ICE configuration validation. WebSocket integration tests
start the real handler with `httptest`, then verify health, create/join,
participant events, opaque description and candidate relaying, duplicate-viewer
rejection, unauthorized relay rejection, token-based reconnect, and room close
after host grace expiry.

The slice is accepted when `go test ./...` passes, the existing C++ core suite
continues to pass, no generated Go output is tracked, and protocol examples
match the server's actual responses. A passing Go integration test does not
claim that native desktop clients, TURN, or public-network calls are verified.
