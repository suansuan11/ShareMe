# Sender Local Video Preview Design

## Goal

The host window must display the exact local video track being sent to the
viewer. For movie calls this replaces the viewer's synthetic grayscale return
video with the decoded movie frame while preserving the viewer's remote movie
display and all existing audio paths.

## Architecture

`SignaledPeer` owns two independent frame sinks. The existing remote sink
continues to observe the received video track. A new local-preview sink observes
the local `VideoTrackInterface` created from the selected source. The peer
attaches the local sink after track creation and removes it before releasing the
track during shutdown.

`RtcDemoController` chooses one callback by role: a host supplies the local
preview callback and a viewer supplies the remote callback. Both callbacks feed
the existing bounded Qt frame-delivery path, so no second decoder, network
loopback, or second `QVideoSink` is needed.

## Constraints

- Do not change movie audio, voice, timeline, signaling, or queue policy.
- Do not decode the movie twice or copy the old debug stash into the branch.
- Never invoke a cleared callback after shutdown.
- Keep the external libwebrtc cache read-only.

## Verification

- A contract test must fail before implementation and prove both callback
  fields and role routing exist after implementation.
- The affected native build and CTest suite must pass.
- A macOS host/viewer smoke using the supplied 4K HEVC/FLAC movie must confirm
  that both windows receive movie-shaped frames; human visual confirmation of
  exact content remains an acceptance boundary unless captured directly.
