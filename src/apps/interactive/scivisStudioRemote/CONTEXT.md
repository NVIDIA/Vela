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
structural scene, layer snapshots, frame config, UI State, Task-Status
Replay, then a Project Snapshot — leaving the client fully populated.
Connecting and reconnecting are the same act; the server's authoritative
state is the session.
_Avoid_: session restore, resync

**Task-Status Replay**:
The part of the Bootstrap that tells a client how every Server Task ended
since the previous Bootstrap (each `TaskCompleted`/`TaskFailed` verbatim) and
which one is running now (one `TaskProgress` naming it). It is what lets a
task outlive the session that launched it: the client fails its open task
records at `BootstrapBegin` and the replay revives the ones the server still
speaks of. Idempotent — an ending heard live may be replayed once more.
_Avoid_: task history sync, task resume

**Exclusive Server Task**:
A Server Task that owns the Project and the Scene while it is queued or
running — in v1 the shot render. Its **pause-and-refuse** rule: interactive
frames pause because the task holds the render loop, and any request that
would mutate the Project or Scene or launch another task is refused with
"render in progress" when it reaches dispatch, rather than held back to run
afterwards; browse, histogram and cancel still go through. An exclusive task
also outlives its session when it is merely queued.
_Avoid_: blocking task, render lock

**UI State**:
The opaque `{windows, layout, settings}` tree a client attaches to
`SaveProject` and receives back in every Bootstrap and after an
`OpenProject` (`UIState`). The server stores and returns it without reading
it, so a layout saved with a project follows the project to whichever client
opens it next.
_Avoid_: layout sync, window settings message

### Client-held state

**Structural Mirror**:
The client's copy of the scene's objects, parameters, and layers without bulk
array contents. Arrays exist client-side as descriptors (type, shape, element
count, value range); its size is a function of project structure, not data
size. Objects and layer nodes keep the server's indices, so a wire identity
(SceneObjectRef, SceneNodeRef) names the same thing on both sides.
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
viewport settings, frame config) from the network IO thread to the render
loop: the handler latches the newest value, the loop applies it once per
iteration. The latch is simultaneously the latest-wins coalescer and the
thread-safety seam. Inputs that must not coalesce — scene edits, and Project
Ops when they arrive — do not go through the latch: they ride an **edit drain
queue** alongside it, kept in arrival order and drained by the same loop
iteration, so nothing is lost and nothing runs off the IO thread.
_Avoid_: message queue for the latch itself (it holds one value, not a
history); latch for the drain queue (it keeps every edit, in order)

**Origin-Based Echo Suppression**:
The rule that an edit never returns to the end it came from: each end
disables its own outbound delegate while applying what the other end sent
(the server while applying client edits, the client while applying pushes
and the Bootstrap). Suppression is decided by where a mutation originated,
never by comparing values.
_Avoid_: change filtering, loop detection

**Latest-Frame-Wins**:
The frame pacing rule: at most one Frame is in flight, and while it is the
server skips rendering rather than queueing pictures, so the client always
shows the newest state and a slow link never builds a backlog.
_Avoid_: frame queue, frame buffer depth

**Pick**:
A request naming a viewport pixel, answered by the server against its current
camera and scene with `{hit, worldPosition, objectIdentity?}`. What the
answer means — focus the camera, select the object — is client UI intent, not
protocol state.
