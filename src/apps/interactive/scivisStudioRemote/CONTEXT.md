# SciVis Studio Remote

The wire vocabulary of the SciVis Studio client-server split: what crosses
the network between the Studio server (which owns the project, scene, and
rendering) and the thin UI client, and what each end holds. Design:
[`docs/scivis-studio-client-server.md`](../../../../docs/scivis-studio-client-server.md).
Project and dataset language is owned by
[SciVis Studio](../scivisStudio/CONTEXT.md) and is never redefined here.

## Language

### Protocol

**Studio Message Set**:
The client-server protocol owned by this context: its own message-type enum
and version, reusing the VSR network transport but sharing nothing with the
remote-viewer demo protocol. A message outside the set is rejected, never
ignored.

**Project Op**:
A synchronous request/reply mutation of project state, one-to-one with a
`ProjectContext` operation, carrying a client-minted request id. The client
applies nothing optimistically; the reply and the following Project Snapshot
are the truth.
_Avoid_: RPC call, command

**Project Snapshot**:
The whole serialized Project, pushed after every confirmed mutation from any
source. It replaces the client's Project Replica wholesale and is the commit
marker: scene messages for the same mutation precede it, and its arrival
means the mutation is fully visible.
_Avoid_: project delta, project patch

**Server Task**:
A long-running server operation identified by a server-allocated task id,
reporting optional progress and exactly one completion or failure, with
cooperative cancel. Single-lane: one task runs, others queue in order. Task
state survives client disconnects.
_Avoid_: background job, async request

**Bootstrap**:
The bracketed message sequence a server sends on every accepted connection —
structural scene, layer snapshots, frame config, UI state, task-status
replay, then a Project Snapshot — leaving the client fully populated.
Connecting and reconnecting are the same act; the server's authoritative
state is the session.
_Avoid_: session restore, resync

### Client-held state

**Structural Mirror**:
The client's copy of the scene's objects, parameters, and layers without bulk
array contents. Arrays exist client-side as descriptors (type, shape, element
count, value range); its size is a function of project structure, not data
size.
_Avoid_: scene copy, full mirror

**Project Replica**:
The client's read-only copy of the real Project value structs, including
runtime-only display fields, replaced wholesale by each Project Snapshot.
Every field may be read and none written; all mutation goes through Project
Ops.
_Avoid_: client project, local project

**Opaque Dataset**:
A dataset as the Structural Mirror holds it: its root node and project
metadata only, with the interior subtree living solely on the server.
On-demand subtree expansion is a reserved protocol affordance, not a v1
feature.

**Connection State**:
The client's explicit state toward its server: `NeverConnected`, `Connected`,
`Lost`, or `Disconnected`. **Lost** is involuntary (view frozen under a
banner, auto-retrying); **Disconnected** is a completed user intention (clean
home state, no retry). The frozen-with-banner treatment is exclusively for
Lost.

### Files

**Data Root**:
A server-launch-configured directory under which all browsing and every
path-taking operation must fall. A guardrail against accidents on a trusted
network, not a security boundary.

**Remote Browse**:
The client's stateless walk of the server's filesystem (`ListRoots`,
`ListDirectory`), replacing native file dialogs. The server lists, the client
filters, and server-side operation validation is the sole authority on what a
path may be used for.

### Time and rendering

**Time at Rest**:
The committed playback position: the replica's `currentFrame` and `playing`,
updated by one Project Snapshot when motion stops.

**In-Motion Time**:
The frame currently shown during playback or scrubbing, carried exclusively
by the per-frame image header (`shotId`, `frame`) and never by the replica.

**Control-State Latch**:
The server pattern marshalling interactive inputs (time, camera, pick,
viewport settings) from the network IO thread to the render loop: the handler
latches the newest value, the loop applies it once per iteration. The latch
is simultaneously the latest-wins coalescer and the thread-safety seam.
_Avoid_: message queue (it holds one value, not a history)

**Pick**:
A request naming a viewport pixel, answered by the server against its current
camera and scene with `{hit, worldPosition, objectIdentity?}`. What the
answer means — focus the camera, select the object — is client UI intent, not
protocol state.
