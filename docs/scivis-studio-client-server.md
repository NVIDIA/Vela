# SciVis Studio client-server split — design spec

This document locks the design for splitting SciVis Studio
(`src/apps/interactive/scivisStudio`) into a headless **server** that owns the
`ProjectContext`, the `vsr::scene::Scene`, and all ANARI rendering, and a thin
**UI client**. It resolves the network protocol, state synchronization, file
access, frame delivery, and a staged implementation plan, and ends with a
sketch of how the two deferred modes — an MPI-distributed server and a
local-loopback mode — fit later.

The load-bearing decisions are recorded as ADRs
([0028](adr/0028-split-scivis-studio-into-a-project-owning-server-and-a-thin-client.md)–[0035](adr/0035-carry-in-motion-time-in-the-frame-header.md));
this document is the connected narrative. The wire vocabulary used throughout
(Structural Mirror, Project Replica, Server Task, Project Snapshot, …) is
defined in the
[SciVis Studio Remote context](../src/apps/interactive/scivisStudioRemote/CONTEXT.md).

## Scope

**In scope (v1):** one server, one client, TCP, trusted network. The full
Studio feature set drives remotely: project/dataset/shot/rig editing, transfer
functions, playback, picking and outlines, and offline shot rendering.

**Out of scope, deliberately:**

- **Multi-client / collaboration.** All authoritative state is server-side and
  any client can bootstrap, so nothing precludes it — but the one-writer
  assumptions below (optimistic one-way edits, origin-based echo suppression)
  are load-bearing, and multi-client returns only as a fresh design effort.
- **Wire file transfer.** No upload, no download, no project-archive export
  over the wire (see [File access](#file-access)).
- **Client-side material editing.** Materials live inside dataset subtrees,
  which v1 keeps opaque; the protocol reserves on-demand subtree expansion as
  the affordance a later material-editing effort needs.
- **MPI and loopback design.** Checked for preclusion only; see
  [How the deferred modes fit later](#how-the-deferred-modes-fit-later).

## Architecture

Two processes over TCP. The **server** is headless: it owns the
`ProjectContext` (Project/Dataset/Shot/Rig CRUD, residency, persistence), the
`Scene`, the `AnimationManager`, all file I/O, and all ANARI rendering — the
project logic moves server-side wholesale (ADR 0028). The **client** owns the
UI and holds two read-oriented copies of server state:

- a **Structural Mirror** of the Scene — objects, parameters, and layers, but
  no bulk array contents (arrays are descriptors: type, shape, element count,
  value range) and no dataset interiors (a dataset is its `rootNode` plus
  metadata — an **Opaque Dataset**). The mirror's size is a function of
  project structure, not data size: that is what makes the thin client thin.
- a read-only **Project Replica** — the real `Project`/`Dataset`/`Shot`/rig
  value structs, serialized through the existing `ProjectSerialization` path
  (including runtime-only fields like `status`/`residency`/`dirty` that the UI
  displays), **replaced wholesale on every confirmed mutation, never patched**.

Authority splits on one line (ADR 0030): *identity, lifecycle, and structure*
are server-authoritative and round-trip; *parameter values on user-editable
objects* (rig-owned lights, cameras, renderers, color-map objects) are
optimistic and one-way. All object identity is minted by the server (ADR
0029): the client never invents a `(type, pool index)`; it learns identities
from creation replies and scene pushes.

Layer structure is server-push-only: there is **no client layer-edit message
in the protocol at all**. The server pushes whole-layer `TransferLayer`
snapshots; the client's `LayerTree` ships as a read-only inspector (a
read-only mode lands in `vsr_ui_imgui`, see the implementation plan).

Echo suppression is by origin: the server's push delegate is disabled while it
applies a client message — the same trick the demo client's
`NetworkUpdateDelegate::setEnabled(false)` plays during scene transfer.
Sequence-number suppression is the documented upgrade path if multi-client
ever lands.

## Transport, session, and server lifecycle

Studio reuses the `vsr::network` transport as-is — Boost.Asio TCP,
`{uint8 type, uint32 length}` message framing, `StructuredMessage` over
`DataTree` (ADRs 0026/0027) — and defines its own message set on top (ADR
0033).

- **Single client per server** in v1.
- **Connect = reconnect.** Every accepted connection gets the full bootstrap;
  the server's authoritative state *is* the session. No session ids, no
  client identity, no delta-resync. On disconnect the server pauses rendering
  and preserves everything, including a running Server Task; the bootstrap
  replays task status (how every task ended since the previous bootstrap, and
  the running one's progress) so a reconnecting client resumes progress
  display seamlessly.
- **Manual launch, CLI-configured.** The user launches the server out-of-band
  (ssh, job script). Configuration is argv only: `--port` (default 12345),
  `--library <anari lib>`, `--data-root` (repeatable), optional
  `--project <path>`. The server is config-free (`~/.config/vsr/studio` is
  frontend-only), outlives client connections, and exits only on an explicit
  `Shutdown` request or a signal. Clients never spawn servers in v1.
- **Hello exchange, exact-match version.** One integer `PROTOCOL_VERSION`
  compiled into both binaries, bumped on any wire-visible change. On accept:
  server sends `Hello{version, build info}`, client replies `Hello{version}`,
  server pushes bootstrap. Any mismatch → `Error` + close. No negotiation, no
  capability flags — both ends live in this repo and are typically built
  together.
- **No authentication in v1.** Deployment stance is a trusted network with
  one user controlling both ends; Data Roots are a guardrail, not a security
  boundary. **ssh port-forwarding is the documented pattern beyond a trusted
  LAN** (and provides encryption). The client `Hello` is the designed slot
  for a shared token later — additive, a version bump, not a redesign.
- **Always a socket.** TCP is the only transport. Loopback is "spawn +
  connect to 127.0.0.1"; no in-process seam is reserved. The MPI mode's rank 0
  exposes this same TCP surface.

### Bootstrap

A **bracketed sequence, not a composite blob**: `BootstrapBegin`; then
ordinary messages — structural scene transfer (descriptor-only arrays), layer
snapshots, frame config, the project's opaque UI-state tree (`UIState`), and
a task-status replay (the `TaskCompleted`/`TaskFailed` of every Server Task
that ended since the previous bootstrap, then one `TaskProgress` for a task
still running); then one `ProjectSnapshot` as the commit marker; then
`BootstrapEnd`. The bracket gives the client its "suppress local reactions,
then refresh everything" window.

## The Studio Message Set

Studio's protocol is its own message-type enum and its own server; nothing is
folded into the remote-viewer demo's enum (ADR 0033). Payload classes from
`src/vsr/network/messages/` are reused under Studio's own type values where
semantics match; Studio-specific payloads start life app-local and graduate
into `vsr::network` only when something else needs them. A message type
outside Studio's set is **rejected with an error**, never silently ignored.

### Request path

- **Granularity is one-to-one with `ProjectContext`'s operations** — a thin
  RPC skin over the already-validated project API. Operations with no API
  today are introduced by the protocol: `SetActiveShot`, shot remove/rename,
  shot bindings.
- **Every project request carries a client-minted request id**, echoed in the
  reply, so multiple windows can have ops in flight.
- **Reply and notification are two messages** (ADR 0034). A sync op is
  answered by a tiny uniform `ProjectOpReply{requestId, ok, error, results}`
  (`results` carries op-specific payloads such as newly allocated ids).
  Separately, **every confirmed mutation, from any source** — client request,
  task completion, server-side effect — fires one whole-`Project`
  `ProjectSnapshot`. Failed ops produce no snapshot. The snapshot carries the
  Project in the model's *Full* serialization form (`projectToNode(...,
  ProjectForm::Full)`: the manifest's fields plus every runtime field inline
  under its entity), and `UpdateShot` nests a `Shot` the same way, so the
  on-disk manifest, the Project Replica and the shot edits share one
  serializer and cannot drift (`PROTOCOL_VERSION` 4).
- **The snapshot is the commit marker.** For one logical mutation the server
  may push scene messages first (object creations, layer snapshots); the
  trailing `ProjectSnapshot` means "this mutation is now fully visible" — one
  well-defined moment for the UI to refresh.

### Message inventory (v1)

- **Session**: `Hello`, `Ping`/`Pong`, `Disconnect{reason?}`, `Shutdown`;
  `BootstrapBegin` / `BootstrapEnd`. `Disconnect` is the last message before
  its sender closes the socket: a client's courtesy `Disconnect` carries no
  reason; the server's farewell names why it ends a session it did not lose
  (v3; "replaced by another client" when a second connection takes over).
- **Project**: `NewProject` (sync), `OpenProject(dir)` (task),
  `SaveProject(dir?, uiState)` (task); `UIState{tree}` (server→client, in
  every bootstrap and after an `OpenProject` completes).
- **Dataset**: static import, subtree-archive import, file-animation import
  (tasks); declared-dataset
  creation (sync — stats nothing, ADR 0023); `ReimportDataset` (task);
  `RenameDataset`, `RemoveDataset(keepAssetFile)`, `UnloadDataset`,
  `RefreshDatasetAvailability` (sync); `LoadDataset`, dataset-archive
  save/load, `IncorporateDatasetCandidate` (tasks);
  `DiscoverDatasetCandidates` (sync, reply carries the candidate list).
- **Shot**: `CreateShot`, `RemoveShot`, `UpdateShot` (whole serialized `Shot`,
  replaced after server validation), `SetActiveShot` (all sync;
  `SetActiveShot` stays separate because it alone triggers
  `applyActiveShot()` side effects).
- **Rig**: create/clone/remove/rename light rig, add/remove light in rig,
  create/remove/rename camera rig, rig-archive save/load (all sync).
- **Color map**: `CreateColorMap{name}` — the server creates **both halves
  atomically** (the `ColorMapRecord` and the scene-side object); the reply
  carries the record id and the object identity. `RenameColorMap` /
  `RemoveColorMap` (sync). Values are optimistic scene parameter edits.
- **Remote Browse**: `ListRoots`, `ListDirectory` (sync).
- **Server Task family**: task-id reply, `TaskProgress`, `TaskCompleted` /
  `TaskFailed`, `CancelTask`. An ending carries what its task produced the
  way a `ProjectOpReply` does, as an opaque `results` subtree holding a
  task-specific payload (a render's `RenderShotResult{framesCompleted}`;
  `PROTOCOL_VERSION` 5).
- **Playback**: `SetPlaying{shotId, bool}` (sync project op),
  `SetTime{shotId, frame}` (optimistic one-way),
  `TimeAdvanceWarning{frame, message}` (server→client).
- **Scene, client→server** (optimistic, no reply): `SetObjectParameter`,
  `RemoveObjectParameter`, `SetNodeTransform` (rig-owned nodes).
- **Scene, server→client**: structural `TransferScene`, `TransferLayer`
  snapshots, object added/removed pushes, `ProjectSnapshot`.
- **Viewport**: `Pick{x, y}` (request/reply),
  `SetOutline{objectIdentity?}` and `ViewportSettings` (optimistic one-way).
- **On-demand**: `RequestArrayHistogram` (sync).
- **Rendering/frames**: frame config, start/stop rendering, per-frame header
  and encoding negotiation (see [Frame delivery](#frame-delivery)).
- **Reserved, not implemented**: dataset-subtree expansion (the
  material-editing affordance), typed-channel frames, an NVENC encoding value.

### Not in the set

With no shared enum, "forbidden" means *absent*: no client→server
add/remove/remove-all object, no array-data upload, no client layer message,
no set-current-camera/renderer (the active shot owns both — one door, not
two), no bare save-state-file, no raw float update-time.

### Encoding and implementation notes

- Project entities are addressed by their **string ids** (`DatasetID`,
  `ShotID`, …), never positional indices; keyed DataTree children use the id
  as the node name (ADR 0025's anonymous-append trap), and every scalar lives
  under a named child (ADR 0027).
- Wire identity for scene objects is the server-minted `(type, pool index)`,
  carried explicitly as the object-reference value a `DataNode` already
  holds for an object parameter, one leaf (`PROTOCOL_VERSION` 5).
  Single-client, the stale-slot window is essentially
  unreachable (every lifecycle event for client-editable objects is a
  round-tripped op the client itself initiated); a generation counter on the
  index is the documented upgrade path if slot reuse ever bites.
- Long-running handlers must not run inline on the asio IO thread (they would
  stall frame sends) — the single-lane Server Task worker owns them, and
  interactive control state is marshalled to the render loop via the
  Control-State Latch (see [Playback](#playback-and-time)).
- Studio's enum avoids the value 255: the demo's `ERROR = 255` collides with
  `MESSAGE_TYPE_INVALID = 255` (`Message.hpp`), an existing bug noted here so
  nobody copies it.
- UI state (windows/layout/settings `DataNode`s) rides `SaveProject` and the
  open/bootstrap path as an **opaque subtree** the server never inspects, so
  a user reconnecting from anywhere gets their layout back.

## File access

**All files live server-side, and no file bytes cross the wire in v1**
(ADR 0031). Import, open, save, and shot rendering all name absolute
server-side paths. The protocol has no bulk file-transfer channel — no upload,
no download, no archive export. This follows the split's premise (data lives
near the compute) and the transport's reality (one contiguous payload, one
write in flight — a large transfer would head-of-line-block frames).

- **Data Roots.** Browsing and every path-taking operation are rooted under
  directories configured at server launch (`--data-root`, repeatable). A
  guardrail, not a security boundary. Paths on the wire are absolute server
  paths, validated against the roots at use.
- **Remote Browse.** Two stateless requests replace the native SDL dialogs:
  `ListRoots` and `ListDirectory(path)` → `{name, kind, size, mtime}` entries.
  The server lists; the client filters (it knows the importer-extension map
  and what a project directory looks like; project directories are marked,
  not hidden). One dumb ImGui remote-browse dialog replaces all three SDL
  dialog modes, keeping the free-text path field as the power-user escape
  hatch. Courtesy hints (overwrite warning, greying out) are advisory;
  **server-side operation validation is the sole authority**.
- **Server Tasks** (ADR 0032). Every long-running operation is answered
  immediately with a server-allocated task id; the server then pushes
  `TaskProgress` events (optional; determinate where the operation can report
  it) and exactly one of `TaskCompleted` / `TaskFailed(error)`.
  `CancelTask(id)` is cooperative. Completion of a project-mutating task also
  fires the `ProjectSnapshot`. The dividing line: touches dataset bytes or
  the disk transactionally → task; metadata edit → sync request/reply.
  **Tasks are single-lane in v1**: one running, others queued, order
  guaranteed. Parallelism is a compatible later change.
- **Shot outputs** stay where `RenderShot` writes them —
  `<project>/renders/<shotId>/` — collected outside the protocol (shared
  filesystem, scp).

## Frame delivery

Grounded in the frame-delivery research (raw 1080p@30 is 2 Gbps against
~0.94 Gbps of 1 GbE goodput; a raw 1080p frame is ~70 ms of pure wire time;
remote links need ≥30:1 reduction).

- **v1 ships two encodings**: `raw` (LAN default) and `turbojpeg`
  (quality 85–95, 4:4:4 chroma to protect text/isolines; the remote default).
- **Every frame message carries a header** — width, height, pixel format,
  encoding tag, plus `shotId` and the integer `frame` the image was rendered
  at (see [Playback](#playback-and-time)). Today's payload is untagged bytes
  validated only by length; the header is the cheapest, most load-bearing
  protocol change.
- **Encoding is negotiated at session setup** (client advertises supported
  decodings, server picks; RFB's SetEncodings is the model) and may switch
  per-frame via the tag — which is what enables the standard
  interactive-lossy / idle-lossless split later without protocol surgery.
- **Latest-frame-wins, one-in-flight pacing stays** (the existing
  drop-if-previous-send-pending scheme; VirtualGL's "frame spoiling").
- **Reserved for v2**: an NVENC HEVC/H.264 encoding value (the only WAN-viable
  path, ~1 ms encode at 1080p, but stateful and color-only — a video mode
  needs its own continuous-substream semantics), and **typed-channel framing**
  for depth/AOV channels, which must ride lossless (LZ4/zstd), never
  JPEG/NVENC.

## Playback and time

The server free-runs playback; the wire speaks integer frames; the frame
header is the sole carrier of in-motion time; the replica carries time at rest
(ADR 0035).

- **Who advances time.** Play/pause is a request; while playing, the server's
  render loop owns `AnimationManager::tick()` on its own steady clock. Only
  the server knows a file-animation frame took 800 ms to load, so only the
  server can pace honestly. The client renders nothing and predicts nothing.
- **Wire unit: integer frames** (plus shot id). `AnimationManager`'s
  normalized float stays a server internal; the lossy conversion lives in
  exactly one place, server-side at the manager boundary. The demo's
  bare-float time messages do not survive into the Studio set.
- **Time rides the frame header** (`shotId`, `frame`), so an image is paired
  with its time by construction — no ordering question can exist.
- **Time at rest vs. in motion.** In-motion time never touches the replica
  (24 whole-project snapshots a second would also destroy the snapshot's
  value as a commit marker). The replica's `currentFrame` means "where time
  rests"; one snapshot commits it when motion stops. Snapshots mark committed
  mutations; in-motion time is playback, not a mutation.
- **`SetPlaying{shotId, bool}`** is a dedicated sync project op (latency-felt
  and side-effectful — it drives the tick loop). **Auto-stop** at the end of
  a non-looping shot is a server-originated confirmed mutation: fix the
  missing manager callback, write the Shot, fire one snapshot
  (`playing: false, currentFrame: last`). The client's play button follows
  the replica and never lies.
- **Scrubbing** is `SetTime{shotId, frame}` — optimistic, one-way,
  latest-wins. The handler latches the requested frame; the render loop
  applies it once per iteration (the **Control-State Latch**, the pattern the
  MPI demo already proves). The latch is simultaneously the coalescer and the
  fix for the demo's real thread hazard (handlers mutating the scene from the
  asio IO thread concurrently with `render()`, unmutexed). Seek during
  playback keeps playing.
- **Never skip frames.** The tick advances at most one frame per render-loop
  iteration; `fps` is a ceiling, not a promise. In scientific visualization
  every frame is the point, and the accumulator catch-up loop is pathological
  with file bindings (it synchronously loads every file it skips past).
- **Load failure keeps playing** (matching today) and pushes
  `TimeAdvanceWarning{frame, message}`, shown non-modally; the same message
  covers a scrub landing on a bad frame.
- **The client has no `AnimationManager` at all.** The adapted timeline UI
  reads the replica (rest) + latest frame header (motion) + local drag state,
  and writes via `SetPlaying` / `SetTime` / `UpdateShot`.

## Picking, selection, and viewport passes

- **Picking is v1**: one unified `Pick{x, y}` request/reply, `x` and `y` in
  frame pixels with `y` down from the top-left corner of the last frame
  config. The server picks against its **current** camera and scene — no
  frame-id correlation, no client rays. Staleness is accepted and self-limiting: users pick at rest,
  and at rest the cameras agree.
- **Reply: `{hit, worldPosition, objectIdentity?}`** — one flat struct.
  `objectIdentity` is the server-minted `(type, pool index)`, absent on
  background; `worldPosition` is computed server-side from its own camera and
  depth. One reply serves both gestures — focus-pick (arcball `setCenter`)
  and object-pick — because **intent is client UI state**. The client
  resolves identity → layer node in its mirrored read-only LayerTree,
  including the not-found → clear-selection fallback.
- **Server mechanics:** the pick is a latched request serviced by the render
  loop at its next iteration, with the objectId/depth channels forced on for
  that one frame. ID channels otherwise stay gated exactly like the
  monolith's `needIDs`. One pick in flight, latest-wins. Pick during
  `RenderShot` is refused ("render in progress").
- **Selection is client UI state, server informed** — never authoritative,
  not in snapshots, saved projects, or bootstrap. The server learns only what
  rendering needs via `SetOutline{objectIdentity?}` — optimistic, one-way,
  latest-wins. Only the first selected object is outlined (as today).
  "Outline" is the established term from `src/vsr/rendering/CONTEXT.md`.
- **The whole id-driven pass suite relocates server-side in v1** — selection
  outline, primitive/box outlines, and the AOV visualization modes all
  composite over the final LDR color buffer *before encoding*, behind one
  flat optimistic `ViewportSettings` message applied via the render-loop
  latch. Zero wire cost; the server pays the channel costs the monolith
  already pays.
- **Wire hygiene:** the wire always carries explicit `(type, index)`; the
  packed volume-high-bit objectId convention (`0x80000000`) stays a
  server-internal detail.
- **v2 room:** when typed lossless channels ship, outline compositing and the
  visual toggles can move client-side again without a protocol break — while
  `Pick` stays server-answered regardless, since the client never holds
  dataset interiors.

## Offline shot rendering

`RenderShot{shotId}` is an ordinary single-lane **Server Task** — nothing
bespoke: immediate task-id reply, **determinate** per-frame `TaskProgress`
(a strict upgrade over today's indeterminate spinner), one
`TaskCompleted`/`TaskFailed`, cooperative `CancelTask` taking effect at the
next frame boundary.

- **Rendering a shot makes it active, and that sticks**: `RenderShot` is
  `SetActiveShot` semantics followed by the render (the engine renders only
  the active shot). The client gets the normal snapshot for the active-shot
  change and can always switch back.
- **Interactive frame delivery pauses** for the task's duration and resumes on
  completion/failure/cancel — forced by the facts: the shot render shares the
  viewport's ANARI device, mutates the shared `Scene` (residency, second
  render index, `applyActiveShot` toggles) and the global `AnimationManager`,
  with no mutex anywhere. Today's app copes identically by disabling the
  viewport.
- **Mutating ops are refused** while the render is queued or running
  ("render in progress"): a mutating request that reaches dispatch then is
  answered with the refusal, not held back to run afterwards, while read-only
  requests (browse, histogram) and `CancelTask` remain fine. Since the render
  body holds the render loop, a request that *arrives* during it is dispatched
  once the body returns — and, the render being over, is then served.
  Cancel-then-edit is the escape hatch.
- **No frame preview in v1** — progress and cancel only. The hook is cheap
  (the frame buffer is in hand right before the PNG write), so a preview
  stream is a clean later addition once the frame header carries dimensions.
- **Outputs** land in `<project>/renders/<shotId>/<prefix>_%04d.png` as today.
  Failed or canceled renders leave partial frames on disk (useful, matches
  today); the final task event carries the frame count reached. Preconditions
  surface as ordinary task failure with the engine's error text — fixing the
  app path that ignores the failure return today.
- **`scivisStudioRenderShot` stays a purely local CLI**, unchanged: it shares
  100% of the render logic via `vsr_scivis_studio_model`, so it cannot drift
  from the server path. The server grows no one-shot render mode in v1.

## Client behavior on server loss

Today's stack has no server-loss handling at all: every asio failure funnels
into `NetworkChannel::log_asio_error`, which logs and notifies nobody. The
spec's model:

- **The client declares loss.** Any socket error is immediate loss. Otherwise:
  the client sends `Ping` after ~5 s of link quiet; no traffic of any kind
  (frames, events, task progress, `Pong`) for ~15 s ⇒ loss. Numbers are
  suggestions, not contract. Safe because sustained *total* silence is never
  a legitimate server state — long work runs as Server Tasks with progress
  events. New machinery: a `Pong` reply and one `asio::steady_timer`; the
  server stays dumb.
- **Single disconnect hook, IO-thread-safe.** One latched disconnect callback
  where all asio failures already converge; it only sets a flag the UI thread
  polls. Never touch UI state or `std::exit` from the IO thread.
- **Freeze in place.** Keep the last frame in the viewport and all panels
  visible but read-only — the mirrors are display data, safe to show stale —
  under one unmissable "server connection lost" banner. No drop to a home
  screen; the user's context costs nothing to keep.
- **Auto-retry, then manual.** The banner appears immediately in a
  "reconnecting…" state while the client retries with backoff for ~a minute,
  then switches to a persistent manual retry prompt. A successful retry heals
  seamlessly (the surviving server preserved everything).
- **Reconnect does nothing special.** Connect = reconnect: handshake +
  bootstrap wholesale-replace the mirrors. A restarted server is honestly a
  first connect.
- **Honest loss window, no v1 autosave.** On a server crash, everything since
  the last explicit save is gone by construction. Server-side periodic
  autosave is the obvious additive follow-up (zero protocol impact), not a v1
  deliverable.
- **Connection-scoped request failure only.** No per-request timeouts: a
  request/reply op fails in exactly one way — loss is declared and **all**
  pending reply-futures fail with "connection lost". Hard requirements:
  declaring loss must fail every pending future, and **no unbounded blocking
  waits on the UI thread** (the demo has three `.get()` calls that would
  deadlock on a wedged-but-open peer).
- **Connection states are crisp:** `NeverConnected`, `Connected`, `Lost`
  (involuntary: frozen view, banner, retrying), `Disconnected` (voluntary:
  clean connect-to-a-server home state, no auto-retry fighting the user's
  intention). The frozen-with-banner treatment is exclusively for `Lost`.

## App and library structure

One new sibling directory — the monolith is untouched, and v1 is additive:

```
src/apps/interactive/scivisStudioRemote/
├── CONTEXT.md            ← wire vocabulary (SciVis Studio Remote context)
├── protocol/             → vsr_scivis_studio_protocol   (OBJECT library)
├── server/               → scivisStudioServer           (executable)
├── client/               → scivisStudioClient           (executable)
└── test_client/          → scivisStudioTestClient       (executable, headless)
```

- **Nothing moves.** `ProjectContext` and the whole project model are already
  libified as `vsr_scivis_studio_model` (flat sources in `scivisStudio/`,
  linked by the monolith, `scivisStudioRenderShot`, and `scivisStudioCLI`).
  It stays put; the new server links it from the sibling directory. One
  change: STATIC → **OBJECT** library (the `vsr_ui_imgui` precedent).
- **Protocol library** lives app-local in `protocol/`, linking `vsr_network`
  plus `vsr_scivis_studio_model` (payloads carry Project state via the
  existing `ProjectSerialization` machinery). Graduates to `src/vsr/network`
  only when something else needs it.
- **Client UI is adapted, not shared.** The monolith's five editors and three
  modals call `ProjectContext` directly; sharing them would force an
  abstraction seam into the monolith now. The client gets its own copies
  under `client/`; duplication is bounded (8 files) and consolidation happens
  when loopback replaces the monolith.
- **Client linkage — wholesale, discipline by review.** The client links
  `vsr_scivis_studio_model` (the replica holds the real structs), which also
  hands it code it must never call. **The client's allowed surface is: types
  and serialization in; `ProjectContext`, persistence, and file I/O out.**
  Splitting the library into types-only + context halves is a later hardening
  step.
- **Build flags:** the new directory is gated behind the existing
  `VSR_USE_NETWORKING` (default OFF). No new flag.
- **ANARI-free client is a design constraint, not a v1 gate.** It is
  impossible today: ANARI is baked into `vsr_core` (the math types are
  ANARI's linalg types; `Any`/`DataTree` use `anari::DataType` tags). An
  ANARI-free client first requires carving math/DataTree out of `vsr_core` —
  a separate refactor this layout must not preclude (the client depends only
  on UI + protocol + model, so it does not). The v1 lightweight-client story
  is `-DVSR_USE_CUDA=OFF`, which already works.

## Headless test client

`scivisStudioTestClient` is a second, complete client with no UI: a
command-line program that speaks the full Studio Message Set from a script so
the server can be exercised end to end on a machine with no display, in CI,
or by an agent working unattended. It exists so that every server-side
milestone can be verified without launching the interactive client, and so
the protocol has two independent client implementations.

- **Its own application, not a test harness for `client/`.** It lives in
  `test_client/`, links only `vsr_scivis_studio_protocol` (and through it the
  transport and model types), and implements its own connection, handshake,
  bootstrap, liveness and loss logic. It shares no code with `client/` beyond
  the protocol library, so a bug in the GUI client's session code cannot hide
  a server bug, and vice versa.
- **Same client-held state.** It maintains a Structural Mirror and a Project
  Replica exactly as the GUI client does, so scripts can assert on what the
  server pushed (object counts, layer counts, active shot, replica fields,
  frame headers), not just on which message types arrived.
- **Scripted, deterministic, machine-checkable.** Commands come from a script
  file (`--script`), inline (`-e "cmd; cmd"`), or stdin, one command per
  line. Every wait has a deadline (`--timeout` default) and every command
  either prints `OK <command>` or `FAIL <command>: <reason>`; server events
  print as `EVT <name> key=value ...` lines. The process exits non-zero on
  the first failure unless `--keep-going` is given. Output is plain text with
  one record per line so a shell script or a reader can grep it.
- **Command vocabulary tracks the message set.** Session (`connect`,
  `disconnect`, `shutdown`, `ping`, `expect-pong`, `await-lost`,
  `reconnect`), rendering (`set-frame-config`, `set-encodings`,
  `start-rendering`, `stop-rendering`, `await-frame`, `save-frame`), scene
  edits (`set-param`, `remove-param`, `set-node-transform`), inspection
  (`dump-scene`, `dump-project`, `dump-layers`), assertions (`assert
  <expr>` over a small set of named values), raw probes (`send-raw <type>
  [payload]`, `expect-error`), and — added by the milestones that introduce
  them — every Project Op, Server Task wait (`await-task`), Remote Browse,
  playback, pick and shot-render command. A command that the server answers
  with `Error` fails the script unless it was written as an expectation.
- **Scenario scripts ship with it.** `test_client/scenarios/*.studio` cover
  each milestone's server surface and are wired into `ctest` (a scenario
  test launches a server on a free port with the CPU ANARI device, runs the
  script, and checks the exit code), skipping cleanly when no device loads.
  Each later milestone adds its scenarios here as its acceptance test.
- **Not a load or fuzz tool.** One connection, sequential commands; no timing
  measurement beyond deadlines. Those stay out of scope.

## Staged implementation plan

Each milestone is independently landable and leaves every existing target
green; everything new sits behind `VSR_USE_NETWORKING`.

1. **Groundwork (no new apps).** Convert `vsr_scivis_studio_model` from
   STATIC to OBJECT. Add the read-only mode to `vsr_ui_imgui`'s `LayerTree`.
   Both benefit the monolith tree without touching behavior.
2. **Protocol library.** `vsr_scivis_studio_protocol`: the Studio message
   enum (avoiding 255), `PROTOCOL_VERSION`, payload types (including the
   frame header, task events, `ProjectOpReply`, `ProjectSnapshot` via
   `ProjectSerialization`), and serialization round-trip tests. No sockets
   needed to land this.
3. **Viewer-parity server + client.** `scivisStudioServer` and
   `scivisStudioClient` at remote-viewer parity: Hello handshake, bracketed
   bootstrap (structural scene, descriptor-only arrays), raw + turbojpeg
   frames with the header and encoding negotiation, optimistic
   camera/parameter edits with origin-based echo suppression, and the
   render-loop Control-State Latch from day one (it is also the thread-hazard
   fix). Ping/Pong, loss detection, freeze-and-retry client states.
4. **Headless test client.** `scivisStudioTestClient` (see
   [Headless test client](#headless-test-client)) with the full milestone 3
   command surface, scenario scripts for the viewer-parity server, and their
   `ctest` wiring. From here on every server milestone lands with the test
   client commands and scenarios that exercise it.
5. **Project layer.** Project Ops one-to-one with `ProjectContext`, request
   ids, `ProjectSnapshot` as commit marker, the Project Replica, Remote
   Browse + the ImGui dialog, the single-lane Server Task worker with
   progress/cancel, and the adapted editor windows against the mirrors.
6. **Time, picking, passes.** Server free-run playback (`SetPlaying`,
   `SetTime`, `TimeAdvanceWarning`, auto-stop snapshot, frame-header time),
   `Pick` via the latch with one-frame ID channels, `SetOutline`,
   `ViewportSettings` with the relocated pass suite.
7. **Shot rendering + hardening.** `RenderShot` as a Server Task
   (pause-and-refuse semantics), task-status replay in bootstrap, `Shutdown`,
   UI-state round-trip through save/open, and an end-to-end pass over the
   loss/reconnect story.

## How the deferred modes fit later

Both deferred modes were checked against every locked decision at a
**protocol-stable bar**: a decision passes as long as the wire protocol and
client behavior survive the mode unchanged — server internals may be
reworked. **All decisions pass.**

### Local loopback

**Endgame.** Frontend + local server eventually *replaces* the monolith (one
codebase, one UI), but that is explicitly not on the v1 roadmap — the monolith
stays until the remote client reaches feature parity. The mechanism is already
locked: separate processes over TCP to `127.0.0.1`, no in-process seam.

**Launch model.** In v1 nothing spawns a server: the user launches it
out-of-band and connects, locally or remotely alike. A frontend-managed
child-process spawn is a plausible later refinement; nothing precludes it
(CLI-only configuration and the `Shutdown` message are exactly the affordances
a spawner needs).

**Fit findings.**

- **Paths.** Absolute server paths under Data Roots work unchanged when both
  ends share a filesystem. Studio path semantics are already anchor-relative
  rather than CWD-relative (ADR 0007; source lists resolve against the list
  file's parent), so same-machine operation introduces no ambiguity. One
  residual CWD fallback exists in `src/vsr/io/importers/PbrtParser.cpp`;
  implementation should not add more.
- **Config.** The server is config-free (CLI flags only);
  `~/.config/vsr/studio` (recent projects, layout) becomes frontend-only. No
  server-side config reads, ever.
- **Ports.** The demo servers hard-code 12345 in five places; `--port`
  already fixes this, and loopback uses any free port.
- **Latency.** No decision assumes remoteness: latest-frame-wins pacing and
  optimistic one-way edits degrade gracefully to near-zero latency; loopback
  simply feels like the monolith.

### MPI-distributed server

**Deferred wholesale to a v2 effort with its own design map.** Checked for
preclusion only; v1 implementation carries no MPI requirements.

Why every decision passes: compositing is the ANARI device's job (rank 0
sends the final frame; the per-frame header, encoding negotiation, and pacing
ride on top unchanged); rank 0 is the sole network gateway (`NetworkServer`
construction stays guarded behind a single "am I the gateway" predicate, as
the demo does); `ProjectContext` naturally lives on rank 0 (strings and
vectors, which `ReplicatedObject` cannot carry; project CRUD never touches
per-rank data), with scene-affecting consequences reaching workers as
broadcast ops; single-lane Server Tasks become collectives the render loop
participates in — single-lane actually *helps* (no concurrent collectives to
schedule); and a **shared filesystem across ranks is a stated assumption** of
the MPI mode.

Known costs recorded for the v2 effort (facts found now so they are not
rediscovered; deliberately not v1 requirements):

1. **Message-application shape.** MPI requires enqueue-and-drain-per-frame
   message application (`MPI_Bcast`/`MPI_Barrier` are collectives all ranks
   hit in lockstep); the demo's mutate-from-IO-thread shape does not port. The
   v1 studio server's Control-State Latch is a step in this direction; if its
   project-op handlers also adopt enqueue-and-drain, the v2 MPI server reuses
   them wholesale — left to the implementer.
2. **File loading distribution.** The existing MPI import path is
   round-robin-by-file-index computed independently and identically on every
   rank; archives are not rank-filtered; rank-0-only save captures a partial
   scene. Project open/import under MPI must be **broadcast-and-locally-
   decide**, never rank-0-loads-then-distributes.
3. **`ReplicatedObject` limits.** POD-only, post-`MPI_Init` construction,
   unconditional per-frame collective pinned to `MPI_COMM_WORLD` root 0.
   Replicable control state must be a flat struct behind deferred
   construction.
4. **The mpiServer demo is plumbing, not a working distributed renderer** —
   it never selects a distributed ANARI device nor passes the per-rank init
   params `mpiViewer` found necessary. Budget for closing that gap.
