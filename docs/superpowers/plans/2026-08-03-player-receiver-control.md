# Player / Receiver Control Slice Implementation Plan

**Goal:** Prove host movie video plus validated playback-state delivery in the
Qt RTC receiver UI.

**Architecture:** Keep WebRTC as an opaque reliable text transport, keep JSON
validation in a QtCore codec/tracker, and reuse the existing movie sources and
receiver video sink.

## Tasks

- [x] Add failing playback-state codec/tracker tests.
- [x] Implement the codec/tracker and register its QtCore test target.
- [x] Add failing `SignaledPeer` control-channel policy/API tests.
- [x] Implement reliable ordered channel creation, receive callbacks, send
      validation, and teardown-safe observer ownership.
- [x] Extend RTC demo CLI tests for movie source combinations and redaction.
- [x] Wire movie sources, periodic host state publication, viewer state
      tracking, and read-only QML status into `shareme_rtc_demo`.
- [ ] Run focused tests, full build/CTest, Go race/vet, skill workflow tests,
      and repository validator.
- [ ] Update verification and current-stage documentation with exact evidence
      and explicit remaining boundaries.
- [ ] Obtain independent specification and quality reviews, repair findings,
      commit, merge to `main`, and push the verified stage.
