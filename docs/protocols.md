# ShareMe Protocols

## Versioning

The initial protocol version is `1`. WebSocket signaling and WebRTC data-channel
messages use the same envelope:

```json
{
  "version": 1,
  "type": "playout-report",
  "roomId": "7K4M9Q",
  "sequence": 42,
  "payload": {}
}
```

| Field | Contract |
| --- | --- |
| `version` | positive integer; reject unsupported major versions |
| `type` | lower-case kebab-case string, at most 48 bytes |
| `roomId` | optional only before room assignment; 6 uppercase base32 characters |
| `sequence` | unsigned JSON-safe integer, monotonically increasing per sender and channel |
| `payload` | object whose schema is selected by `type` |

Receivers reject messages over 64 KiB, malformed UTF-8, unknown required
fields, invalid field types, and identifiers outside their documented limits.
Unknown message types produce an `unsupported-message` error and do not close a
healthy connection.

## Signaling Transport

The WebSocket endpoint is `/v1/ws`. Authentication uses a short-lived token in
the connection request. Tokens and full SDP descriptions are never written to
normal logs.

### Room Messages

`create-room`

```json
{
  "version": 1,
  "type": "create-room",
  "sequence": 1,
  "payload": {
    "role": "host"
  }
}
```

`room-created` returns `roomId`, a short-lived participant token, and ICE
servers. `join-room` carries the invitation code and role `viewer`.
`participant-joined`, `participant-left`, and `room-closed` are server events.
One active host and one active viewer are allowed.

### WebRTC Negotiation Messages

`session-description`

```json
{
  "version": 1,
  "type": "session-description",
  "roomId": "7K4M9Q",
  "sequence": 9,
  "payload": {
    "descriptionType": "offer",
    "sdp": "v=0..."
  }
}
```

`descriptionType` is `offer`, `answer`, or `rollback`. SDP is opaque to the
server and limited by the envelope size.

`ice-candidate`

```json
{
  "version": 1,
  "type": "ice-candidate",
  "roomId": "7K4M9Q",
  "sequence": 10,
  "payload": {
    "candidate": "candidate:...",
    "sdpMid": "0",
    "sdpMLineIndex": 0
  }
}
```

An empty `candidate` marks end-of-candidates. The server validates sizes and
relays the payload without parsing network addresses.

`restart-ice` requests a new negotiation generation. Reconnects include the
last received sequence so the server can distinguish a resumed participant
from a duplicate connection.

## Data Channel

The reliable ordered control channel is named `shareme-control-v1`. Metrics may
later use a separate unordered channel; it is not part of the foundation.

### Playback State

```json
{
  "version": 1,
  "type": "playback-state",
  "roomId": "7K4M9Q",
  "sequence": 35,
  "payload": {
    "state": "playing",
    "mediaPtsMs": 125000,
    "effectiveAtHostTimeMs": 9864891,
    "rate": 1.0,
    "generation": 4
  }
}
```

`state` is `playing` or `paused`. Seek increments `generation`; messages from an
older generation are ignored. `rate` is in `[0.5, 2.0]` for validation, though
automatic correction uses only `[0.98, 1.02]`.

### Viewer Playout Report

```json
{
  "version": 1,
  "type": "playout-report",
  "roomId": "7K4M9Q",
  "sequence": 42,
  "payload": {
    "renderedPtsMs": 125430,
    "bufferMs": 160,
    "receiveTimeMs": 9865321,
    "generation": 4
  }
}
```

The viewer sends a report every 250 ms while media is active. Positions and
times are signed 64-bit milliseconds. `bufferMs` must be in `[0, 10000]`.
`receiveTimeMs` comes from a monotonic local clock and is used only with other
samples from the same peer.

### Sync Decision

The host does not need to transmit every local correction. A hard resync or seek
uses `sync-command`:

```json
{
  "version": 1,
  "type": "sync-command",
  "roomId": "7K4M9Q",
  "sequence": 43,
  "payload": {
    "action": "hard-resync",
    "targetPtsMs": 125600,
    "generation": 5
  }
}
```

## Sequence and Error Handling

- Duplicate sequence numbers are ignored after acknowledging the last accepted
  value.
- A lower sequence is stale unless it belongs to an explicitly resumed
  connection generation.
- Gaps are recorded. Reliable signaling/control gaps trigger state
  reconciliation rather than guessing missing state.
- Errors use `code`, `message`, `retryable`, and optional `relatedSequence`.
- Error messages contain no token, SDP, ICE credential, or full device ID.

## Media Tracks

Track identifiers are stable within a peer connection:

| Identifier | Direction | Content |
| --- | --- | --- |
| `movie-video` | host to viewer | movie or fallback shared video |
| `movie-audio` | host to viewer | stereo movie audio |
| `host-voice` | host to viewer | mono processed microphone |
| `viewer-voice` | viewer to host | mono processed microphone |

Media payload is encrypted by WebRTC and never encoded into signaling messages.
