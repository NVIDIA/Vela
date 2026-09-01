# Confirm mutations with a whole-project snapshot

A Studio project operation is answered by a tiny uniform reply
(`ProjectOpReply{requestId, ok, error, results}`), and — separately — every
confirmed project mutation from any source (client request, Server Task
completion, server-originated effects such as playback auto-stop) fires one
notification carrying the **whole serialized `Project`**: the
`ProjectSnapshot`. The client replaces its Project Replica wholesale on every
snapshot; nothing is ever patched. Failed operations produce no snapshot. For
one logical mutation the server may push scene messages first; the trailing
snapshot is the **commit marker** — "this mutation is now fully visible."

Per-entity delta notifications were rejected because they turn replica
consistency into a class of bugs: every new field or entity type adds a delta
message someone can forget, order incorrectly, or apply against a stale base.
`Project` is a handful of vectors of small value structs — kilobytes for a
realistic project — so wholesale replacement is affordable and makes
divergence structurally impossible rather than merely unlikely. Deltas remain
a compatible refinement behind the same notification if profiling ever
justifies them.

Folding confirmation into the reply (one message instead of two) was rejected
because mutations do not only come from replies: task completions and
server-side effects also change the project, and the client would need a
second update path anyway. One snapshot channel means the UI has exactly one
refresh trigger and one consistency sentence. The snapshot is also why
in-motion playback time stays out of the replica (see ADR 0035): a
per-frame snapshot storm would destroy the snapshot's meaning as a commit
marker.
