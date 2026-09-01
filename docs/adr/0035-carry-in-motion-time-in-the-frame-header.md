# Carry in-motion time in the frame header

During Studio client-server playback, the server free-runs the clock and the
integer frame currently being shown travels in exactly one place: the
per-frame image header (`shotId`, `frame`). The Project Replica's
`Shot.currentFrame` and `Shot.playing` carry **time at rest** — one
`ProjectSnapshot` commits them when motion stops (pause, seek-at-rest, or
server-originated auto-stop at the end of a non-looping shot). In-motion time
never touches the replica, and the wire never speaks the `AnimationManager`'s
normalized float — that conversion stays server-internal.

This is a rate-scoped split of one value across two channels, which is
surprising enough to record. The alternatives both fail concretely. Pushing
time through the replica during playback means a whole-project snapshot per
frame — tens per second — which drowns the snapshot's role as a mutation
commit marker (ADR 0034) and re-sends kilobytes to move one integer. Sending
time as a separate stream beside frames (the demo's bare-float
`SERVER_UPDATE_TIME`) reintroduces the pairing problem this design closes:
a frame is only meaningful paired with the time it was rendered at, and the
demo's client provably cannot know which time a received image shows. Putting
the pair in the header makes the association true by construction — no
ordering question can exist.

The consequence for the client is honest simplicity: it has no
`AnimationManager` at all. The timeline UI reads the replica for rest, the
latest frame header for motion, and local drag state while scrubbing; it
writes via `SetPlaying` (a sync project op) and `SetTime` (optimistic,
latest-wins). Nothing on the client ticks, so no follower clock can drift.
