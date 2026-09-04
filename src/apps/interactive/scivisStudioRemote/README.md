# SciVis Studio Remote

The client-server split of SciVis Studio: `scivisStudioServer` owns the
project, the scene and the ANARI device and streams frames; the thin
`scivisStudioClient` shows them and edits the scene through a Structural
Mirror. Design: [`docs/scivis-studio-client-server.md`](../../../../docs/scivis-studio-client-server.md);
vocabulary: [`CONTEXT.md`](CONTEXT.md).

```
scivisStudioRemote/
├── protocol/   vsr_scivis_studio_protocol   Studio Message Set, codecs, frame codec
├── server/     scivisStudioServer           headless render loop + Control-State Latch
├── client/     scivisStudioClient           ImGui UI over the client core
└── test_client/ scivisStudioTestClient      headless scripted client + scenario tests
```

## Build

Everything here is gated behind `VSR_USE_NETWORKING` (Boost.Asio, default
OFF). JPEG-compressed frames need `VSR_USE_TURBOJPEG` (default OFF, only
offered with networking on; `find_package(libjpeg-turbo CONFIG)` must
succeed). Raw frames always work, so a build without turbojpeg still
interoperates with one that has it.

```bash
cmake -S . -B build -DVSR_USE_NETWORKING=ON -DVSR_USE_TURBOJPEG=ON \
  -Dlibjpeg-turbo_DIR=<prefix>/lib/cmake/libjpeg-turbo
cmake --build build --parallel --target scivisStudioServer scivisStudioClient
```

The server needs no SDL or ImGui; the client needs `VSR_BUILD_UI_LIBRARY`.
`-DVSR_USE_CUDA=OFF` is the lightweight client build.

## Run the server

```bash
scivisStudioServer --data-root /data/sims [--data-root ...] \
  [--project /data/sims/demo] [--library helide] [--port 12345]
```

- `--data-root` (repeatable): the Data Roots every path the server touches
  must fall under. At least one is required; with only `--project`, the
  project directory's parent becomes the root, and a `--project` directory
  outside every root is admitted as one more root. Roots and requested paths
  are canonicalized (symlinks resolved) and compared by path component, so a
  request naming `/data2/x` is refused under the root `/data`.
- `--project`: open an existing project directory; otherwise the server
  starts on a fresh unsaved project with one shot.
- `--library`: the ANARI library to render with (`helide`, `visgl`, ...).
  Omitted, the first loadable entry of the device manager's list is used
  (`VSR_ANARI_LIBRARIES` orders that list); an unloadable library falls back
  through the rest. The library's `.so` must be on `LD_LIBRARY_PATH`.
- `--port` (default 12345).

Configuration is argv only; the server reads no config file. It keeps running
across client disconnects and exits on `Ctrl-C`/`SIGTERM` or when a client
chooses *Server > Shutdown Server*.

## Run the client

```bash
scivisStudioClient [--host 127.0.0.1] [--port 12345] [--connect]
```

`--connect` connects at startup; otherwise use *Client > Connect*. The
*Server* menu selects the frame encoding (turbojpeg when both ends have it),
starts and stops rendering, and shuts the server down. Losing the server
freezes the last frame under a banner while the client retries with backoff;
a restarted server is simply reconnected to and bootstraps the client again.
Details of what a loss keeps and a reconnect rebuilds are under "Loss and
reconnect" below.

### Over ssh

There is no authentication: the server trusts its network. Beyond a trusted
LAN, forward the port through ssh and connect to loopback:

```bash
ssh -L 12345:localhost:12345 user@render-host \
  'LD_LIBRARY_PATH=... scivisStudioServer --data-root /data --port 12345'
scivisStudioClient --host 127.0.0.1 --port 12345 --connect
```

## What the server covers so far

Milestone 3, remote-viewer parity (item 3 of the spec's
[staged implementation plan](../../../../docs/scivis-studio-client-server.md#staged-implementation-plan)):
Hello handshake with exact-match `PROTOCOL_VERSION`, the bracketed Bootstrap
(structural scene with descriptor-only arrays, layers, frame config, UI
state, task-status replay, Project Snapshot), raw and turbojpeg frames with header and encoding
negotiation, optimistic camera and parameter edits with origin-based echo
suppression, the render-loop Control-State Latch, Ping/Pong liveness, and
the `NeverConnected`/`Connected`/`Lost`/`Disconnected` client states.
Milestone 4 added the headless test client.

Milestone 5, the project layer (item 5): every Project Op of the message set
(types 20..57), Remote Browse and `CancelTask`, served on the server side by
`server/ProjectOpDispatcher` over new whole-operation `ProjectContext` calls
(`removeShot`, `updateShot`, `setActiveShot`, the color map trio). Requests
are decoded on the IO thread, queued beside the edit drain queue and
dispatched on the loop thread in the order sent: a sync op replies then
snapshots when the Project changed (also after a "failed" call that still
mutated it, such as an import that left an `ImportFailed` record); a task op
replies with its task id and its body runs later as a Server Task. At
dispatch a task op checks only the paths it names against the Data Roots;
whatever it reads from the Project (a dataset id, the project's own directory
for a `SaveProject` without one) it reads when the task runs, since a task
queued ahead of it may still change the Project. A sync op the client sent
after a task waits until that task has run; task requests do not wait --
except `RenderShot`, whose sync prelude reads the Project and so waits like
a sync op -- and `CancelTask` and Remote Browse are served even from behind
a waiting sync op (they touch neither Project nor Scene).

Server Tasks (`server/ServerTaskRunner`) run on the render loop, one per
iteration, to completion; frames pause while one runs (the client keeps its
last frame). `TaskProgress` is indeterminate with phase text for every task
but the shot render, whose progress is determinate (`current`/`total` =
frame/frames). `CancelTask` removes a task still queued
(`TaskFailed{"cancelled"}`); for the running task it is honoured only by a
body that polls its cancel flag (today the shot render, see milestone 7) --
the flag is raised on the IO thread the moment the `CancelTask` is decoded,
the body stops at its next frame, and the `CancelTask` itself, dispatched
once the body has returned, is answered "ok" because the body reported it
stopped short. A body that completed regardless of the flag (every other
task), or failed for a reason of its own, was not cancelled: that
`CancelTask`, like one for any task that already ended, is
refused with "task already finished" for as long as the runner's history
holds the ending (the last 32); one for an id never issued gets "unknown
task N". A body that throws (a filesystem
error on a path the roots admitted) fails its task rather than the server.
Queued tasks die with the session they were sent on -- except a queued shot
render, which like a running one outlives its session and runs with nobody
listening -- and the client fails its open task records with "connection
lost" at every `BootstrapBegin` for the same reason; the task-status replay
inside the bracket revives the ones the server finished or is still running:
the runner keeps the last 32 endings and the bootstrap sends each one not
replayed before, `TaskCompleted`/`TaskFailed` verbatim, between `UIState`
and the `ProjectSnapshot`, then a `TaskProgress{message = description}` for a
task running at that moment. `OpenProject` goes through `stageProjectOpen` then
`ProjectContext::openStagedProject`, both on the loop thread, so a later
worker-thread staging phase is a mechanical move. Task ids increase for the
life of the server; `TaskCompleted::message` of an import carries the new
dataset's id, that of a render its output directory.

Data Roots (`server/DataRoots`) gate every path-taking op: open and save
project, imports, archive save and load, dataset candidates and Remote
Browse. The requested path is made canonical (`weakly_canonical`, so the
existing prefix has its symlinks resolved) and must lie inside one root by
path components; a path outside is refused with an error reply naming it
before anything is touched. The `--project` directory is implicitly a root.
Remote Browse (`server/RemoteBrowse`) lists a directory inside the roots as
`File`, `Directory` or `ProjectDirectory` (a directory holding the project
manifest) with sizes and mtimes; a file, a missing directory or a path
outside the roots is refused.

Milestone 6, time, picking and passes (item 6): `SetPlaying` is a Project
Op and `SetTime` a Control-State Latch slot; the server free-runs the
`AnimationManager` on its loop thread, commits Time at Rest through snapshots
(auto-stop, debounced scrub) and reports frames a file binding cannot load
as `TimeAdvanceWarning`. `Pick`, `SetOutline` and `ViewportSettings` are
latch slots (latest-wins); `RequestArrayHistogram` is a sync Project Op. See
"Playback" and "Viewport" below.

### Playback

- **One frame per tick.** Every loop iteration applies the latch, ticks the
  `AnimationManager` with a steady-clock delta, renders, and sends a `Frame`
  whose header carries the frame *after* the tick: the frame actually
  rendered (Time in Motion travels in headers, never in snapshots).
  `AnimationManager::tick` advances **at most one frame per call** and
  returns whether time moved; the shot's `fps` is a ceiling, and the
  accumulator is clamped so a slow render never catches up by skipping
  frames. Ticking goes on while a `Frame` send is still in flight, so time
  does not freeze on a slow link. The scene pass renders synchronously
  (`AnariSceneRenderPass::setRunAsync(false)`: render, wait and composite in
  one call), so the pixels are the scene after the tick, not the previous
  iteration's; the same holds for the frame a pick reads its depth from.
- **Wire pacing (v1 behaviour).** "Never skip frames" is a guarantee about
  rendering and time advance, not about wire delivery. Frames are
  latest-frame-wins on the wire (a rendered frame whose send would overlap
  the previous one is dropped) and again in the client core's single slot,
  so at a period near the wire's round trip (fps 1000 over a real socket)
  consecutive headers can read `1 2 4`. At the fps of a real shot (the
  scenarios use 24-30) the loop iterates several times per frame and every
  frame reaches the client; a client process stalled for longer than a frame
  period still sees a skip in its slot, which is why `playback.studio` runs at
  25 fps and the strict never-skip check lives in the in-process E2E test.
- **Time at Rest is committed by snapshot.** `SetPlaying{shotId, bool}`
  (the active shot only, else an error reply) replies then snapshots;
  stopping writes `playing=false` and the frame it stopped on into the Shot.
  A non-looping shot that plays off its end auto-stops: the manager's
  playback-stopped callback lets `ProjectContext` write `playing=false,
  currentFrame=last` into the active Shot and the loop sends one unrequested
  snapshot when it sees the flip. While paused, a `SetTime` shows in the
  next frame at once and commits **debounced**: one snapshot once no
  `SetTime` has arrived for 250 ms (`SCRUB_COMMIT_QUIET`) and the frame
  differs from the one the scrub started from; a `SetTime` for a shot that
  is not active is logged and ignored; while playing, `SetTime` moves time
  but nothing is committed, and a pending scrub commit is dropped once the
  shot plays. An `UpdateShot` landing while the shot plays (a Loop, Frames or
  FPS edit from the Timeline) keeps the frame in motion: the server ignores
  the incoming `currentFrame` as it ignores `playing`, and the Timeline sends
  the replica's resting frame rather than the last header's.
- **`TimeAdvanceWarning`.** A `FileBinding` that cannot load a frame reports
  `{frame, message}` to its `AnimationManager`, which records it against the
  clock frame being applied (the shot frame the Timeline shows; a binding
  with fewer files than the shot has frames reports its own file index, and
  the manager boundary is the one place that converts); after every tick or
  `SetTime` the loop drains `takeLoadFailures()` and pushes one
  `TimeAdvanceWarning{shotId, frame, message}` per failure. Load failure
  never stops playback. The record holds at most
  `AnimationManager::MAX_LOAD_FAILURES` (256) so a driver that never drains it
  (the monolith) does not grow it forever.
- **The manipulator follows client camera edits.** A `SetObjectParameter`
  landing on the active shot's camera object updates the server's
  `m_ctx.view.manipulator` from the camera pose (`followCameraEdit`), and a
  camera rig without keyframes has its `current` view follow too. So
  `applyActiveShot()`, which re-samples the camera from the rig on every time
  change, writes the client's own pose back instead of a stale one, and a
  scrub or `SetPlaying` never snaps the view; a rig with keyframes drives the
  camera during playback as designed. An orthographic shot camera is followed
  through its `height` and `position` too (`Manipulator::setFixedDistancePose`),
  since `updateCameraObject` derives both from the manipulator's distances.

### Viewport

- **Pixel convention.** `Pick{x, y}` names a pixel of the last `FrameConfig`
  in frame pixels, `x` to the right and `y` *down* from the top-left corner
  (the client's image origin). The server converts to ANARI's bottom-up
  buffer; coordinates outside the frame are clamped to its edge. One pick is
  in flight at a time, latest-wins: a `Pick` that arrives before an earlier
  one was serviced replaces it and only the survivor is answered. Servicing
  renders one frame with the `objectId` channel on (also while paused, when no
  `Frame` follows) and replies `PickReply{hit, worldPosition, objectIdentity}`
  before the next `Frame`: `hit` when the pixel shows a surface or volume,
  `objectIdentity` its `{ANARI_SURFACE|ANARI_VOLUME, pool index}`, and
  `worldPosition` the camera position plus the depth along the pixel ray
  built from the shot camera object's `position`/`direction`/`up`/`fovy` (or
  `height` for an orthographic camera) and the frame aspect.
- **Pass order.** `server/ViewportPasses` appends, between the
  `AnariSceneRenderPass` and the copy-out pass and in the monolith
  Viewport's order: `PickPass`, `VisualizeAOVPass`,
  `PrimitiveOutlineRenderPass`, `OutlineRenderPass`, `BoxOutlineRenderPass`.
  The `objectId` channel is on exactly while an outline shows, the AOV is
  `EDGES` or `OBJECT_ID`, or the primitive outline is on (or a pick is being
  serviced). `primitiveId` is queried once at startup
  (`ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID`); without it the primitive outline
  and the `PRIMITIVE_ID` AOV stay silently off. `ViewportSettings` is applied
  whole (absent fields mean defaults), `SetOutline` of anything but a surface
  or volume clears the outline, and both reset to defaults when a new client
  connects. World bounds come from the render index world's `bounds`
  property every frame the box is shown.
- **Histogram limits.** `RequestArrayHistogram` bins a scalar host array on
  the loop thread (frames pause for the duration; linear in the element
  count): `binCount` is clamped to `[MIN_HISTOGRAM_BINS, MAX_HISTOGRAM_BINS]`
  (`[1, 4096]`, declared once in `protocol/ViewportMessages.h` for both
  ends), the last bin is closed, and equal min and max put everything in bin
  0. Fixed-point element types count in ANARI's normalized range. NaN and
  infinite elements take no part in the range or the bins and are counted in
  `ArrayHistogramResult::nonFinite` (an array with no finite element reports
  range `(0, 0)` and empty bins). Refused with an error: references that are not
  arrays, proxy arrays (the mirror's descriptors), CUDA arrays and non-scalar
  element types (vectors, matrices, object handles). No snapshot follows.

Milestone 7, shot rendering and hardening (item 7): `RenderShot` (60) is
served; every type a client may send is now either handled or refused with a
specific reason. A request whose payload cannot be decoded is refused loudly
rather than dropped: with a `ProjectOpReply{ok=false}` carrying the request's
id when the payload has a readable non-zero `requestId` (so a client's
pending request retires), and with a bare `Error{"malformed ..."}` otherwise.
The client core covers the second case too: a bare `Error` that names the
type of a pending request fails the oldest pending request of that type, so
no control stays greyed until the connection is lost. `PROTOCOL_VERSION` is
2: `TaskFailed` gained `framesCompleted` (optional on the wire).

### Shot rendering

- **A Server Task with a sync prelude.** `RenderShot{shotId}` is refused at
  once when the shot does not exist, when the project is not saved ("project
  is not saved; save it before rendering") or when a render is already queued
  or running ("render in progress"). Otherwise the shot becomes the active
  one, the pipeline rebinds (which also pins the shot's renderer settings to
  the server's library, as every bind does) and a `ProjectSnapshot` follows
  the `TaskStartedResult` reply, so the client sees the switch before the
  first frame renders. The body is `renderActiveShotToFrames` -- the same
  engine path as `scivisStudioRenderShot` and the monolith -- with a per-frame
  hook: `TaskProgress{current = frame, total = frames, message = "frame N of
  M"}` before each frame. `TaskCompleted{message = output directory,
  framesCompleted}` ends it; the directory is `<project>/renders/<shotId>/`,
  inside the Data Roots by construction and listable with `ListDirectory`.
  Preconditions the engine finds (a dataset that cannot be made resident, a
  missing camera) fail the task with the engine's text. A snapshot follows
  either way: the render restores residency, the dirty flag and the frame
  time it found -- so rendering a shot that was already active leaves a saved
  project saved, while the switch to a shot that was not active is an edit
  like any `SetActiveShot` and leaves the project dirty.
- **Cancel and Shutdown.** `CancelTask` naming the running render raises the
  runner's cancel flag on the IO thread; the body stops before its next frame
  (granularity: one frame times `samples` renders), the task ends
  `TaskFailed{"cancelled", framesCompleted}`, and the `CancelTask` reply is
  "ok" once dispatched after the body returns (for a task that completed
  regardless of the flag it is "task already finished"). Frames already
  written stay on disk. `Shutdown` stops a running render the same way. A
  `CancelTask`
  that reaches the IO thread in the instant between the render leaving the
  queue and its body starting misses the flag and is answered "task already
  finished" after the render completes; the window is a few microseconds.
- **Pause-and-refuse.** Interactive frames pause by construction (the body
  holds the loop thread) and resume after the task. While a render is queued
  or running, every request that mutates the Project or Scene or launches a
  task -- including a second `RenderShot` -- is refused with "render in
  progress" when dispatched, and is not held back behind the render to be
  served later; `ListRoots`, `ListDirectory`, `RequestArrayHistogram` and
  `CancelTask` are served. A `RenderShot` sent behind queued tasks (a
  `SaveProject`, an `OpenProject`) waits for them like a sync op, since its
  prelude reads the Project; its body fails with "shot 'X' is no longer
  active" should the shot it named not be the active one when it runs.
  Requests that *arrive* while the body runs are latched and dispatched
  after it, when the render is over, so they are served normally. Scene
  edits, `SetTime` and `Pick` latched during the body targeted a scene the
  render was mutating: the edits and the scrub are dropped with a log line,
  the pick is answered `Error{"Pick N refused: render in progress"}` -- the
  moment the body returns, before its ending goes out, so anything sent on
  hearing the ending is served.
- **Sessions.** A render survives its session, queued or running: the body
  runs with nobody listening and the next bootstrap's task-status replay
  reports how it ended. A client that connects mid-render is bootstrapped
  after the body returns; until then it may receive live `TaskProgress` for
  an id it never launched (the GUI shows it as "Task N" until the replay
  names it). The `ProjectSnapshot` of that bootstrap shows the Project the
  render left. Endings delivered live during a session are replayed once
  more at the next bootstrap (the history is "since the last bootstrap"),
  which the clients treat as idempotent.
- **Second client.** One client per server: a connection accepted over a
  live session replaces it. The replaced client is sent the farewell
  `Disconnect{"replaced by another client"}` (v3) before its socket closes
  (`NetworkServer::setReplaceHandler`; the transport gives the old
  connection's write queue up to 200 ms to drain before closing it and
  announcing the new connection) so its banner names the reason; on a link
  too slow to drain a queued Frame in that time the client sees the plain
  close.
- **UI state round trip.** The server keeps the `{windows, layout,
  settings}` tree of the project it opened: from `--project` at startup
  (`setupProject` reads it with the same out-params the dispatcher uses),
  from `OpenProject` (whose body sends `UIState{tree}` before its
  `TaskCompleted` and snapshot, so the client that asked can apply the
  opened project's layout), and from a `SaveProject` that carried one.
  `SaveProject` writes the client-supplied tree, or the retained one when
  the request has none, so a headless save never drops a layout. Every
  bootstrap sends `UIState` (null when no project with UI state was ever
  opened) before the task-status replay.

A fresh project (server start without `--project`, or `NewProject`) reports
`dirty == false` in its snapshot: binding the server's renderer into a shot
that never picked one completes the shot's defaults and is not counted as
an edit; overriding a real pick (an opened project saved for another
library) is.

## The client's editors (milestone 5)

The client carries its own copies of the Studio editors under
`client/windows/` (Project, Dataset Editor, Shot Editor, Light Rig, Camera
Rig, plus a Tasks panel) and modals under `client/modals/`. They read the
Project Replica and send Project Ops through `ProjectOps`; nothing is applied
optimistically -- a control with a request in flight is greyed until the
reply, and the snapshot behind an accepted reply is what the panels show.
Reply errors and Server Task outcomes go to the Log window and a transient
toast. Every path is a server path chosen in the Remote Browse dialog
(`client/RemoteBrowseDialog.*`), which lists the server's Data Roots and
marks project directories; the free-text path field is the escape hatch and
the op that consumes the path is the authority.

*File* holds New/Open/Save/Save As (Ctrl+S saves; Save As and Open go
through the Project Location dialog), *Studio* holds Add Dataset (Static,
File Animation) and Add Shot. Save attaches the `{windows, layout, settings}`
UI-state tree in the monolith's shape; see "UI state" below for how it comes
back.

### Time, picking and passes (milestone 6)

The client has no `AnimationManager`. The **Timeline** window
(`client/windows/Timeline.*`) is the monolith's transport row and ruler
without tracks: Play/Pause sends the `SetPlaying` project op and the button
follows the replica alone; Stop is `SetPlaying(false)` then `SetTime 0`;
dragging or clicking the ruler and the frame field send `SetTime`, at most
one per UI frame with the latest value; Loop, Frames and FPS travel as
`UpdateShot`. The frame shown is the drag while scrubbing, the last frame
header's frame while the shot plays (Time in Motion), and the replica's
`currentFrame` otherwise (Time at Rest). Space toggles playback while the
Timeline is focused. A `TimeAdvanceWarning` becomes a toast and a Log line,
never a modal.

In the viewport, a double-click picks the object under the mouse (`Pick`,
frame-header pixels with y down from the top-left) and selects it in the
Layers window through the mirror; Shift+double-click re-centres the arcball
on the hit point. Selection stays client-local: whenever the first selected
node's object changes and is a surface or volume it is sent as `SetOutline`.
The viewport's *View* menu holds the AOV combo (all names; the server decides
whether PRIMITIVE_ID is available), the depth range and edge inversion,
Highlight Selected, Outline Primitives and World Bounds with colour and
width; every change sends the whole `ViewportSettings`, which persist in the
window's UI state and are re-sent after every bootstrap.

The **Histogram** window (`client/windows/HistogramPanel.*`) lists the array
parameters of the first selected object (and of a volume's spatial field),
takes a bin count and asks the server with `RequestArrayHistogram`; the
reply is plotted, a refusal shows the server's error text.

### Rendering, task records, UI state, loss (milestone 7)

**Render Shot.** The Shot Editor's *Render Shot...* confirms the frame count
and the output directory (`renders/<shotId>/` under the project) and sends
`RenderShot` (`ProjectOps::renderShot`). The server makes the shot active
and renders it as a Server Task; the reply is refused, and toasted, when the
project is unsaved or a render is already queued or running (the button
also notes an unsaved project). While a render this client launched is
active every editor shows a "Render in progress" note, since the server
refuses edits until it ends; an edit that slips through is refused with the
usual toast. The Tasks panel draws the render's per-frame progress as a
determinate bar (`current/total`), shows the frame count and the output
directory when it completes, and offers *Cancel* for Running tasks as well
as Queued ones: the server decides, stopping a render at its next frame
(the frames rendered so far stay on disk) and refusing anything else.

**Task records** (`ProjectOps::TaskRecord`). A record is created by the
reply that starts a task (labelled after the request) or by the first event
naming an unknown id: a `TaskProgress` labels it with its message, which is
how the bootstrap's replay names the task still running when a client
reconnects; a replayed `TaskCompleted`/`TaskFailed` of a task nobody here
launched is labelled "Task N". At `BootstrapBegin` every Queued or Running
record is failed with "connection lost", marked `announced`; a
`TaskCompleted`/`TaskFailed` overwrites the record in place, replayed inside
the bracket or not (same label, the server's ending). A `TaskStarted` reply,
and a `TaskProgress` for a record that finished, start the record over
(label, state, progress, outcome and `render`, `generation` bumped): a
restarted server counts ids from 1 again, so an id that already finished
here names a new task, and the replay's progress for a task this client
failed is the same case (the record takes the replay's description as
label). `render` is the one field a start-over keeps, and only on a record
this client failed at `BootstrapBegin` (`announced`): the server never
ended that task, so a render it named may still be running and still
refusing edits, and the editors must go on saying so. Records the
server never mentions again stay Failed until "Clear finished" -- they were
queued tasks the server dropped with the old session. Completion toasts
fire once per `{generation, state}` change, replayed outcomes included; the
client's own "connection lost" failures do not toast (the banner said it,
which is what `announced` records) and do not count as announced, so the
ending the replay brings for that task still does.

**UI state.** The bootstrap's `UIState` is applied in `onBootstrapComplete`
exactly as the monolith applies a loaded project's: `windows/<name>` through
each window's `loadSettings` (the window names match the monolith's, so a
project saved by either restores the other's viewport and per-window
settings), `layout` through `ImGui::LoadIniSettingsFromMemory`, and
`settings/{fontScale, uiRounding}` through `loadApplicationSettings`. A null
tree keeps the current layout. It is applied only when the client has no
live layout of its own -- the first bootstrap out of the home state
(NeverConnected or Disconnected) -- not on the re-bootstrap a reconnect after
Lost runs, which would yank the layout the user is looking at. A `UIState`
outside a bootstrap follows an `OpenProject` this session asked for and is
applied at once, as opening a project in the monolith does. Save sends the
current tree; the viewport settings the tree loads are sent to the server
right after, by `onServerReady()`.

**Loss and reconnect** (`ServerConnection`). A loss keeps the mirror,
replica and last frame as a frozen read-only view; pending requests fail
once with "connection lost"; task records are handled at the next
`BootstrapBegin` as above. Between a reconnect's Hello and its
`BootstrapBegin` the client is `Connected` but not `bootstrapped()`: the
replica on screen is the previous session's, so the editors stay read-only
(`EditorContext::canSend`; the Object and Database editors' lock is released
in `onBootstrapComplete`) -- a wait that lasts as long as the render a busy
server finishes before it bootstraps anyone. A loss *during* a bootstrap empties the mirror
instead of leaving the part that arrived (the replica is still the previous
session's, since its snapshot comes last in the bracket). A retry greeted
with a mismatched protocol version ends in `Disconnected` with the mismatch
as the status text -- the server that came back cannot be talked to, so the
banner offers no retry. When the server closes a session itself it says why
first with `Disconnect{reason}` (its farewell, v3), and that reason is the
one the banner shows for the loss that follows; that is how a client evicted
by a second client learns why (a close with no farewell shows the socket's
reason, "End of file"). Inbound messages are handled before the close latch
in each poll so a farewell is never lost to the close it explains.

## Hardening dispositions (milestone 7)

Every question left open by the earlier milestones has a disposition here:
**fixed**, **v1 behaviour** (kept and documented), or **deferred** with the
reason. Nothing is silently open.

### Design questions

- **Color map objects** -- *deferred.* No scene object type carries a color
  map as parameters: vsr_ui_imgui's TransferFunctionEditor edits a
  `vsr::scene::Array` of RGBA samples (`colormap_<name>`), so the server
  pairs each `ColorMapRecord` with an `ANARI_FLOAT32_VEC4` Array of 256
  default samples named `<colorMapId>_colormap`; the name is the only link.
  The samples are array data, not parameters, so `SetObjectParameter` cannot
  edit them, the descriptor-only `TransferScene` does not carry them, and an
  opened project recreates each record's Array with default samples. The v1
  message set has no array-data upload (the spec's "Not in the set"), so
  transfer-function editing waits for a `SetArrayData`-style addition and a
  version bump; the client shows color maps read-only.
- **Renderer library switches** -- *v1 behaviour.* `UpdateShot` accepts a
  `renderSettings.rendererObjectIndex` only when it names a Renderer of
  `renderSettings.rendererLibrary` (or is unset). The server renders with its
  own library regardless and rewrites the active shot's renderer settings to
  match when they disagree, as `setupRendering` always did, so a client
  choosing another library sees its choice overridden in the next snapshot
  rather than a device switch. `RenderShot` is safe under this rule because
  its prelude rebinds (`finish(..., rebind)`) before the body reads the
  shot's library.
- **Task threading** -- *deferred.* Tasks run on the loop thread (frames
  pause) rather than on a worker; the split `stageProjectOpen` /
  `openStagedProject` prepares the move of the disk phases and nothing on the
  wire changes when it happens.
- **Light rename, camera-rig clone, camera-rig keyframe editing** --
  *deferred.* The v1 message set has no op for renaming a light node (node
  and object names are not parameters), cloning a camera rig, or editing a
  camera rig's keyframes and current pose (the monolith's Set View, Capture,
  Update, Delete, pose editor and inline frame/name/interpolation edits). The
  client shows these read-only with a tooltip saying so. Candidates for the
  next version bump: `RenameLightNode{lightRigId, lightNode, name}`,
  `CloneCameraRig{cameraRigId}`, and an `UpdateCameraRig{rig}` whole-value
  replace mirroring `UpdateShot`. `UpdateShot` covers every Shot field except
  `playing`, which stays with `SetPlaying`.
- **Naming a loaded Dataset Archive** -- *deferred.* `LoadDatasetArchive`
  carries no name, so the Add Static Dataset dialog applies a typed name with
  a follow-up `RenameDataset` once the snapshot after the load shows exactly
  one new dataset id (`client/ArchiveRenameFollowUp`). A `name` field is a
  wire change; it rides the next version bump together with the rig ops
  above.
- **Renderer libraries in the Shot Editor** -- *deferred.* The client offers
  the device names of the Renderer objects in the Structural Mirror (plus the
  shot's current value); the server's loadable-library list is not in the
  protocol. A `ServerInfo` bootstrap message (or `Hello.buildInfo` structure)
  would carry it -- same bump.

### Issues carried across milestones

- **`SceneNodeRef.nodeIndex` on sparse layers** (M3, found in M4) --
  *fixed* in the M4 fix-up: `TransferLayer` preserves the server's node
  indices, so a `SetNodeTransform` from either side names the same node.
- **Bare `Error` left a GUI request pending** (M5) -- *fixed* in M5 T5:
  the server answers with `ProjectOpReply{ok=false}` whenever the payload
  carried a request id, and the client retires the oldest pending request a
  bare Error names.
- **Client-generated `CreateShot` name** (M5) -- *fixed* in M5 T5: the
  client sends an empty name and the server numbers the shot.
- **`CancelTask` on a finished task said "unknown task N"** (M5) --
  *fixed*: the runner's finished-task history distinguishes "task already
  finished" from an id never issued. The M7 fix-up keeps that history across
  bootstraps (each replay used to clear it, so the distinction held only
  until the next one), up to its 32-entry cap.
- **Cancelling a lone queued task races the loop iteration that runs it**
  (M5) -- *v1 behaviour.* Single-lane, one task per iteration: a task queued
  by one iteration runs in the same one, so a `CancelTask` sent right after
  the `TaskStarted` reply usually finds it running (honoured by a render,
  "task already finished" for the rest). Scenarios shield a task they cancel
  behind a running one; the GUI's Cancel simply reports what the server said.
- **Frame headers skipping at fps 1000 over a socket** (M6) -- *v1
  behaviour*, see "Wire pacing" above: never-skip is a guarantee about
  rendering and time advance, not wire delivery.
- **`StudioScenario.playback` flake under `ctest -j 8`** (M6) -- *fixed*
  in the M6 fix-up: the scenario runs at 25 fps and the strict never-skip
  check lives in the in-process E2E test.
- **`vsr::CameraArchive` failed once under `ctest -j 8`** (M4) --
  *deferred (watch)*: unrelated to networking, never reproduced since.
- **`vsr::StudioClient` heap corruption once under `ctest -j 8`** (M5) --
  *deferred (watch)*: the fixture joins the fake server before destroying
  client state; not reproduced in the five-run Studio-tag sweeps of M6 and
  M7.
- **A `send()` racing a connection replacement lands on the next
  connection** (M7 code-quality sweep) -- *deferred (watch)*: `send()`
  queues without a socket generation, so a message sent just as the peer
  closes and a new connection is accepted can be written to the new one.
  (`start_next_write` does check `m_socketGeneration` at write completion;
  applying it at enqueue time is the real fix, and is not small enough to
  have ridden along with the farewell work.) Seen once, under a `ctest -j4`
  sweep, as the test-client fake answering a courtesy `Disconnect` with
  `Error "Disconnect is not served by the fake"` on the connection the
  script's `reconnect` had just made; not reproduced in the single-suite
  runs or the following sweeps. The mechanism pre-dates the farewell work
  (at 1111d0d the fake already answered unhandled types that way and
  `disconnect()` already sent the courtesy `Disconnect`), and `send()`
  dispatching rather than posting now closes the observed window, since the
  fake's `Error` is enqueued inline in the read handler; a `send()` from a
  thread other than the IO thread still posts, so the race remains in
  principle.
- **Second client evicts the first, then both fight through auto-retry**
  (M6 notes) -- *v1 behaviour*, narrowed: the server still takes the newest
  connection, but tells the evicted client why (`Disconnect{"replaced by
  another client"}`) so its banner names the cause; two clients pointed at one
  server will still take turns. Refusing the second connection instead is a
  one-line policy change in `NetworkServer::setReplaceHandler`'s caller if
  it is ever wanted.
- **A cancelled render toasts as "failed: cancelled"** (M7 notes) -- *v1
  behaviour*: the task ended as `TaskFailed{"cancelled"}` and the toast says
  so; the Tasks panel row shows the frames written (the client core now
  takes `framesCompleted` from `TaskFailed` as well as `TaskCompleted`).
- **Duplicated request-type lists and small copies across client files**
  (M5 review) -- *deferred* to a cleanup ticket; each list gained
  `RenderShot` in this milestone.

### Milestone 7 review fix-ups

Findings of the M7 review, all *fixed* on `m7/fixups`:

- `CancelTask` on a running task whose body ignores the flag (every task
  but the render) was answered "ok" after the task completed; now "task
  already finished" -- a cancel counts only when the body stopped short.
- The finished-task history was cleared by each replay; it is kept (marked
  replayed) so a cancel of an ended task is told so after a bootstrap too.
- Inputs latched during a render were discarded at the next loop iteration,
  a window that also swallowed a `SetTime` or `Pick` sent on hearing
  `TaskCompleted`; the discard now happens the moment the body returns.
- `RenderShot` was dispatched ahead of tasks sent before it (a never-saved
  project's `SaveProject`; an `OpenProject` that would replace the shot); it
  waits for them, and the body checks the shot it named is still active.
- A throw from the render's frame loop leaked the render index and a device
  retain and left the shot's time and playing unrestored; scope guards.
- Both clients: a restarted server reusing a finished record's task id left
  the new task showing as finished; the launch reply or first progress
  starts the record over.
- GUI: the Object and Database editors unlocked at Hello, before the
  reconnect's bootstrap; a replayed real failure could go un-toasted when
  the bootstrap batch split across polls.

Code-quality follow-ups to the M7 review:

- The four request-classification predicates (`waitsForQueuedTasks`,
  `independentOfQueuedTasks`, `refusedWhileRendering` and the file-local
  `queuesWithoutReadingProject`), each an `isOneOf` chain over the 42-way
  `ProjectRequest`, are derived from one `RequestPolicy` row per alternative
  (`launchesTask`, `readsProjectAtDispatch`, `mutates`; the table in
  `server/ProjectOpDispatcher.cpp`). The table was transcribed from the
  predicates and checked equal to them for every alternative before they
  went, with one deliberate exception: `DiscoverDatasetCandidates` scans the
  datasets directory and reports no change, so its `mutates` is false and it
  is served during a render, as `RequestArrayHistogram` already was; the old
  `refusedWhileRendering` list refused it. Adopting a candidate
  (`IncorporateDatasetCandidate`) still is refused. `RefreshDatasetAvailability`
  keeps `mutates` because it writes `dataset->status`. A new alternative
  without a row fails to compile.
- The render refusal is decided in one place: `ProjectOpDispatcher::refuses`
  (`renderActive() && policyOf(request).mutates`). `dispatch` answers it
  with "render in progress" and `StudioServer::dispatchPendingRequests` asks
  it so a doomed request does not wait behind the queue; the free function
  `refusedWhileRendering` is gone. The post-render latch discard has one
  path, `Host::dropLatchedInputs`, called from a scope guard inside the
  render body so it precedes the ending message however the body leaves --
  the last frame, a cancel, or a throw out of a frame's load or encode, which
  `runTaskBody` rethrows to the runner's catch; the `RanTask::exclusive` flag and the
  `std::optional<RanTask>` return of `runOneTask()` built for a follow-up
  after the body were never consumed in production and are removed
  (`runOneTask()` is `void` again; the runner's `runOne()` still returns the
  record the snapshot decision reads, `RanTask` then, `FinishedTask` since
  the cancellation item below).
- The project's UI state crosses `ProjectContext` as one
  `{windows, layout, settings}` tree, the shape the manifest, the wire
  `UIState` and the appliers already shared: `saveProject`, `openProject`
  and `openStagedProject` take a single nullable `DataNode *` in place of
  the `windows`/`layout`/`settings` triple, `ProjectSaveRequest` carries
  `uiState`, and the persistence code copies only those three children
  (an empty layout is still not written), so the manifest is unchanged for
  the same inputs -- with one exception, below. The server's `UIStateCapture` and the save handler's
  `uiStateParts` are gone: an open writes into `makeSubtree()->root()` and
  a save passes `&tree->root()`. Applying a tree lives once, in
  `vsr::ui::imgui::Application::applyUIStateTree`, called by the base
  class's session load, the monolith's `openProject` and the client's
  `applyUIState` (still a null check, still between NewFrame and Render,
  still behind `m_layoutLive`). The applier skips missing children where
  the base session load and the monolith used to load every window from an
  empty `windows` node, and skips an empty layout string where the base
  session load used to hand it to ImGui. Session files always carry
  `windows` and `SaveIniSettingsToMemory` is never empty, but project
  manifests reach the skip: every `saveProject(dir, nullptr, ...)` writes
  none of the three keys, `StudioCLI`'s `persistProject` and `project init`
  among them. Opening one is still a no-op, because `DataNode::getValue`
  leaves the destination alone when the node has no value, and the two
  side effects that would have fired are
  `camera_setUseImplicitAspectRatio(same value)` and
  `StudioViewport::loadSettings`'s trailing `sendViewportSettings()`, a
  no-op until the server is ready. `child()` is also the right guard here:
  `root["windows"]` would create the child in the snapshot being applied.
  The `m_appSettingsDialog->applySettings()` call after the
  session load stays there: neither the monolith nor the client made it
  after an open.
- The one manifest difference: a headless save of a project whose manifest
  had no UI keys writes two fewer keys than before. The server's old
  `UIStateCapture` built its tree with `operator[]`, so an open created
  empty `windows` and `settings` children; a later `SaveProject` with no
  client tree (the test client, any headless caller) fell back to that tree
  and `buildProjectSavePlan` wrote both as valueless leaves. The open now
  guards with `child()`, so the tree stays empty and neither key is
  written. Accepted as a fix rather than reverted: the output matches what
  a `nullptr` save writes, and nothing in-tree requires the keys (open
  guards with `child()` too). "Manifest byte-identical" therefore held for
  the GUI path, which always supplies real windows.
- Cancellation is reported, not inferred. `TaskResult::cancelled` says the
  body stopped because its `TaskControl` reported a cancel request; the
  render body sets it from `RenderShotResult::cancelled` (the wire error
  stays the string `"cancelled"`). The runner used to guess it from "the
  flag was raised and the body did not succeed", which took a render that
  failed for a reason of its own after a cancel for a successful cancel;
  now that `CancelTask` is refused with "task already finished". A task
  dropped from the queue keeps `cancelled == false` (the handoff's sketch
  had it true, which would have acknowledged a repeat cancel and broken an
  existing test): no body ran, the `CancelTask` that dropped it is answered
  "ok" on the spot, and a later one is told the task finished, as before.
  `FinishedTask` is
  `{taskId, description, result, replayed}` and `RanTask` is gone:
  `runOne()` returns the `FinishedTask` it recorded. The running task's id
  lives only in the atomic the IO thread reads (`RunningTask` lost its
  `taskId`), and the test-only `ServerTaskRunner::cancelRequested(id)` is
  gone; the test observes the flag through `TaskControl` inside a body.
- Both files that crossed 1000 lines in M7 are split along seams that were
  already there. `server/ProjectOpDispatcher.cpp` (1115 lines) keeps the
  request plumbing, the policy table, the sync-op handlers, Remote Browse
  and `CancelTask` (686 lines); the task-launching handlers -- every
  alternative whose `RequestPolicy::launchesTask` is set -- with
  `startTask`, `runTaskBody` and the two helpers only they use
  (`datasetNotFound`, `importResult`) moved unchanged to
  `server/ProjectOpDispatcherTasks.cpp` (466 lines), a second translation
  unit of the same class. The one helper both halves read, the dataset
  status before/after a load or unload, became the private static
  `ProjectOpDispatcher::datasetStatus` rather than a copy in each anonymous
  namespace. `client/Application.cpp` (1025 lines) lost the toast text
  `watchTasks` built to `TaskRecord::describeEnding()`, a pure function of
  the record that the unit tests now cover, and the toast queue with the
  connection-lost banner drawing to `client/StatusOverlay.{h,cpp}`. The
  overlay knows nothing of the connection: `drawLostBanner(autoRetrying,
  statusText)` returns the button pressed and the Application acts on it,
  so Retry and Disconnect behave as before (947 lines). `applyUIState` had
  already shrunk to the null check plus the base-class applier with the
  UI-state item above.
- The GUI client's `TaskRecord::stale` ("failed by the client at
  BootstrapBegin, not by the server") drove a revival in `recordFor`, a
  `freshRecordFor` that revived and then overwrote, a `fresh` choice between
  the two in `handleTaskProgress`, and a skip in `Application::watchTasks`;
  the headless test client models the same server with none of it. The
  client now has the test client's rule: `recordFor` finds or creates,
  `startOver` resets a record as if newly heard of and bumps
  `TaskRecord::generation`, a `TaskStarted` reply always starts over,
  progress for a finished record starts over, an ending overwrites in place.
  What the toast needed from `stale` is `TaskRecord::announced`, set only by
  `failUnfinishedTasks` and cleared by any word from the server;
  `watchTasks` skips such a record and keys what it announced on
  `{generation, state}`, so a task restarted under a reused id toasts again
  even when the restart and the ending land in one poll. One visible
  difference, ratified rather than reverted: a replayed `TaskProgress` for a
  record the client failed relabels it with the replay's description instead
  of keeping the launching request's label (a replayed ending keeps it, as
  before). Keeping the label unconditionally would show the previous task's
  label when a `TaskStarted` carries an empty `taskLabel` under a reused id,
  which `freshRecordFor` never did. `render` is kept, though: dropping it
  let a reconnect during this client's own render clear the editors' "the
  server refuses edits until it ends" note and re-enable "Render Shot..."
  while the server still refused; `startOver` carries the flag when the
  record it restarts is `announced`.
- The server evicting a client for a newer one sent a bare `Error` through
  a transport escape hatch (`NetworkChannel::sendImmediately`, a non-blocking
  write past the queue with one caller), and the GUI client paired any bare
  `Error` with a close inside two seconds to make it the loss reason (two
  members and a timestamp comparison). Protocol v3 gives the server an
  explicit farewell instead: `Disconnect` carries a `reason` and goes both
  ways (a client's courtesy `Disconnect` leaves it empty; it is still not in
  `isServerToClient`), the replace handler sends it with the ordinary
  `send()`, and `NetworkServer` lets the replaced connection's queue drain
  (200 ms at most, polled on the IO thread; a `stop()` in that window drops
  the waiting replacement and re-arms the accept) before closing it and
  adopting the new socket, so there is one write path. Both clients take the
  loss reason from the farewell alone, with no timing involved; a bare
  `Error` followed by a close is a toast plus the socket's reason again. The
  other server-initiated closes keep their shape: a Hello with the wrong
  version is answered with `Error` and closed (the spec's "Error + close";
  the reply to a message, not a farewell), a malformed Hello gets an `Error`
  and the connection stays, and a `Shutdown` or a client's own `Disconnect`
  is a close the client asked for. Two transport-side decisions came with
  this and were ratified in review. `NetworkChannel::send()` dispatches its
  enqueue rather than posting it, so a `send()` on the IO thread has queued
  before it returns; the guarantee is documented on the declaration, it
  degrades to a post off the IO thread, and it holds no lock across the
  call, so the inline `enqueue_write` cannot deadlock. And the drain is
  bounded at `REPLACE_DRAIN_TIMEOUT` (200 ms, matching the clients'
  `COURTESY_SEND_TIMEOUT`) polled every `REPLACE_DRAIN_POLL` (5 ms): a
  Frame mid-write on a link that cannot drain in time loses the farewell
  and the evicted client sees "End of file", the pre-v3 behaviour. A
  completion-driven drain would remove the poll, if it ever seems worth the
  code in a shared transport.
- Two boundary clean-ups. `renderActiveShotToFrames` returns its
  `RenderShotResult` by value (the `bool` it returned was the result's
  `completed`, and the optional out-param made the body alias a local); the
  monolith's CLI reads `.completed`, the dispatcher and the unit tests take
  the value. A throw from the frame loop therefore yields no result -- the
  dispatcher never read one on that path (`runTaskBody` catches), and the
  unit test that read `framesCompleted` after the throw now checks the one
  frame file on disk instead. The test client's `await-task` used to fill
  `$lastDatasetId` from any completion message starting `dataset_`, a prefix
  sniff on the server's id format that M7 introduced when a render's message
  became its output directory; `taskStarted(TaskMessage)` now records at the
  launch reply what each task's message will carry (`DatasetId` for the four
  dataset-producing commands: `import-static-dataset`,
  `import-file-animation-dataset`, `load-dataset-archive`,
  `incorporate-dataset-candidate`) and `await-task` consults that record.
  `$lastTaskMessage` is set as before. A task the runner did not launch
  (`await-task <id>` on an id from another session) fills only
  `$lastTaskMessage`; no scenario does that.

### PR review fix-ups

Findings of the 2026-09-03 code-quality review of the whole branch against
`main`, each *fixed* on the branch:

- **One table for the message set.** `StudioMessageType`, `toString`,
  `isStudioMessageType` and `isServerToClient` were four hand-kept lists of
  the same 78 rows (about 400 lines) with nothing checking they agreed. An
  X-macro `STUDIO_MESSAGE_TYPES` in `protocol/StudioProtocol.h` now holds
  each row as (enumerator, wire value, direction) and everything else is
  derived: the enum, a `constexpr` `MESSAGE_TYPE_TABLE`, and the three
  public functions, whose names and results are unchanged (a dump of name,
  validity and direction for all 256 values matched before and after; no
  `PROTOCOL_VERSION` bump). A `static_assert` rejects a table that assigns
  0, 255 or a duplicate value, and the `[StudioProtocol]` suite checks every
  row's `toString` is its enumerator and that the table's size matches the
  test's hand-written list, so a lost row fails on one side or the other.
  `MessageDirection` is `Both` for the session messages (Hello, Error,
  Ping/Pong, Disconnect), otherwise `ClientToServer` or `ServerToClient`;
  the direction column is what the client and test-client dispatch switches
  can read next.

### Spec conformance

Every bullet of the spec sections named below, against the tree at
milestone 7. *Implemented* means as written; *partial* means part of the
bullet is deliberately not there yet; *deviates* means the behaviour differs
and the difference is a recorded decision (the milestone 7 README's numbered
decisions, `M7-n`).

| Spec bullet | Status | Where | Note |
|-------------|--------|-------|------|
| **Message inventory (v1)** | | | |
| Session: `Hello`, `Ping`/`Pong`, `Disconnect{reason?}`, `Shutdown`, `BootstrapBegin`/`End` | implemented | `protocol/SessionMessages.h`, `server/StudioServer.cpp` | `Error` (2) is the bare error the spec's "rejected with an error" needs; `Disconnect.reason` (v3) is the server's farewell to an evicted client |
| Project: `NewProject`, `OpenProject`, `SaveProject(dir?, uiState)` | implemented | `server/ProjectOpDispatcher.cpp`, `server/ProjectOpDispatcherTasks.cpp` | plus `UIState` server-to-client (107), sent in every bootstrap and by `OpenProject`'s body |
| Dataset ops (imports, declare, reimport, rename/remove/unload/refresh, load, archive save/load, incorporate, discover) | implemented | `ProjectOpDispatcher.cpp` (sync), `ProjectOpDispatcherTasks.cpp` (tasks), types 23..35 | task/sync split as listed |
| Shot: `CreateShot`, `RemoveShot`, `UpdateShot`, `SetActiveShot` | implemented | types 36..39 | `UpdateShot` never honours `playing` |
| Rig: light rig create/clone/remove/rename, add/remove light, camera rig create/remove/rename, archives | implemented | types 40..52 | |
| Color map: `CreateColorMap` both halves atomically, `RenameColorMap`, `RemoveColorMap`; values optimistic parameter edits | **partial** | types 53..55, `ColorMapCreatedResult` | the samples are Array data, not parameters, so no optimistic edit reaches them; see "Color map objects" (deferred) |
| Remote Browse: `ListRoots`, `ListDirectory` | implemented | `server/RemoteBrowse.cpp` | |
| Server Task family: task-id reply, `TaskProgress`, `TaskCompleted`/`TaskFailed`, `CancelTask` | implemented | `server/ServerTaskRunner.cpp`, `protocol/TaskMessages.h` | `TaskFailed.framesCompleted` added (v2); the bootstrap replays endings since the previous bootstrap, not only the running task (M7-4; spec paragraph tightened) |
| Playback: `SetPlaying`, `SetTime`, `TimeAdvanceWarning{frame, message}` | implemented | `protocol/PlaybackMessages.h` | the warning also names the `shotId` |
| Scene client-to-server: `SetObjectParameter`, `RemoveObjectParameter`, `SetNodeTransform` | implemented | `protocol/SceneEditMessages.h` | |
| Scene server-to-client: `TransferScene`, `TransferLayer`, object added/removed, `ProjectSnapshot` | implemented | `protocol/SceneMessages.h`, `ProjectSnapshot.h` | |
| Viewport: `Pick`, `SetOutline`, `ViewportSettings` | implemented | `protocol/ViewportMessages.h` | |
| On-demand: `RequestArrayHistogram` | implemented | `server/ArrayHistogram.cpp` | |
| Rendering/frames: frame config, start/stop, header, encoding negotiation | implemented | `protocol/FrameMessages.h`, `FrameCodec.h` | |
| Reserved, not implemented: subtree expansion, typed channels, NVENC | implemented (reserved) | `FrameMessages.h` comment | no value defined for any of the three |
| **File access** | | | |
| No file bytes cross the wire | implemented | -- | no upload/download/export message exists |
| Data Roots: `--data-root`, absolute paths validated at use | implemented | `server/DataRoots.cpp` | canonicalized, compared by component; `--project` is implicitly a root |
| Remote Browse replaces the SDL dialogs; server lists, client filters; project directories marked | implemented | `server/RemoteBrowse.cpp`, `client/RemoteBrowseDialog.cpp` | `{name, kind, size, mtimeSeconds}` |
| Server Tasks: immediate id, progress, one ending, cooperative cancel, snapshot on completion, single-lane | implemented | `server/ServerTaskRunner.cpp` | tasks run on the loop thread, not a worker thread: frames pause (M5 design, "Task threading" deferred) |
| Shot outputs stay in `<project>/renders/<shotId>/` | implemented | `scivisStudio/RenderShot.cpp` | listable with `ListDirectory` |
| **Frame delivery** | | | |
| Two encodings: raw and turbojpeg (quality 85-95, 4:4:4) | implemented | `protocol/FrameCodec.cpp` | quality fixed at 90, `TJSAMP_444` |
| Every frame carries a header (size, format, encoding, shotId, frame) | implemented | `FrameMessages.h` | |
| Encoding negotiated at session setup, may switch per frame via the tag | implemented | `SetEncodings`, `StudioServer.cpp` | `SetEncodings` is accepted at any time; the server never switches on its own in v1 |
| Latest-frame-wins, one in flight | implemented | `StudioServer::renderAndSendFrame` | also a single slot in the client core |
| Reserved for v2: NVENC, typed-channel framing | implemented (reserved) | -- | |
| **Playback and time** | | | |
| Server advances time on its own clock; client predicts nothing | implemented | `StudioServer::tickPlayback` | |
| Wire unit is integer frames plus shot id | implemented | `PlaybackMessages.h` | |
| Time rides the frame header | implemented | `FrameHeader.frame` | |
| Time at rest in the replica, in motion in headers | implemented | `StudioServer.cpp`, `client/windows/Timeline.cpp` | |
| `SetPlaying` sync op; auto-stop is a server-originated snapshot | implemented | `ProjectOpDispatcher.cpp`, `ProjectContext` callback | |
| Scrubbing is optimistic `SetTime` through the latch; seek while playing keeps playing | implemented | `StudioServer::applyTime` | rest commit debounced 250 ms |
| Never skip frames | implemented | `AnimationManager::tick` | about rendering and time advance; wire delivery is latest-wins (v1 behaviour, "Wire pacing") |
| Load failure keeps playing, `TimeAdvanceWarning` | implemented | `StudioServer::pushLoadFailures` | |
| The client has no `AnimationManager` | implemented | `client/` | |
| **Picking, selection, and viewport passes** | | | |
| One `Pick{x, y}` against the current camera and scene | implemented | `StudioServer::servicePendingPick` | pixels y-down from the top-left (spec now says so) |
| Reply `{hit, worldPosition, objectIdentity?}` | implemented | `PickReply` | |
| Latched, serviced next iteration with id channels forced; one in flight; refused during `RenderShot` | implemented | `StudioServer.cpp` | a Pick latched during the render body is answered `Error{"Pick N refused: render in progress"}` |
| Selection is client state; `SetOutline` informs the server | implemented | `client/Application.cpp`, `server/ViewportPasses.cpp` | |
| Pass suite server-side behind `ViewportSettings` | implemented | `server/ViewportPasses.cpp` | `PRIMITIVE_ID` silently off without the extension |
| Wire carries explicit `(type, index)` | implemented | `SceneObjectRef` | |
| v2 room for client-side compositing | n/a | -- | nothing precludes it |
| **Offline shot rendering** | | | |
| `RenderShot` is an ordinary Server Task: id reply, determinate progress, one ending, cancel at the next frame | implemented | `ProjectOpDispatcher::handle(RenderShot)` | exclusive task (M7-1, M7-2) |
| Rendering a shot makes it active, and that sticks | implemented | same | the switch is an edit (dirty), as `SetActiveShot` is |
| Interactive frame delivery pauses and resumes | implemented | by construction: the body holds the loop | |
| Mutating ops refused while the render runs; read-only fine | **deviates** | `ProjectOpDispatcher::dispatch`, `StudioServer::dispatchPendingRequests` | refused when they reach dispatch while a render is queued or running (M7-3); a request that *arrives* while the body holds the loop is dispatched after it returns and, the render being over, served. The spec sentence now says so. |
| No frame preview in v1 | implemented | -- | |
| Outputs `<project>/renders/<shotId>/<prefix>_%04d.png`; partial frames kept; frame count in the ending; preconditions as task failure | implemented | `RenderShot.cpp`, `TaskFailed.framesCompleted` | |
| `scivisStudioRenderShot` unchanged, shares the engine path | implemented | `scivisStudio/RenderShot.cpp` | result struct extended, CLI behaviour kept |
| **Client behavior on server loss** | | | |
| Client declares loss: socket error, or Ping after ~5 s quiet and ~15 s silence | implemented | `client/ServerConnection.cpp` | `ConnectionTimings` defaults 5 s / 15 s |
| Single IO-thread-safe disconnect hook, UI thread polls | implemented | `ServerConnection::onChannelClosed`/`poll` | |
| Freeze in place under a banner | implemented | `client/Application.cpp`, `client/StatusOverlay.cpp` | mirror, replica and last frame kept; a loss during a bootstrap empties the mirror (M7-6, item 8) |
| Auto-retry with backoff for ~a minute, then manual | implemented | `ServerConnection::poll` | a retry greeted by another protocol version ends `Disconnected` with the mismatch text and no retry offer (M7-6, item 12) |
| Reconnect does nothing special; a restarted server is a first connect | implemented | bootstrap | task records are failed at `BootstrapBegin` and revived by the replay (M7-4); the UI layout is not re-applied on the re-bootstrap after Lost |
| No v1 autosave | implemented | -- | |
| Connection-scoped request failure only; no UI-thread blocking | implemented | `ProjectOps::failAllPending` | |
| Crisp states `NeverConnected`/`Connected`/`Lost`/`Disconnected` | implemented | `ConnectionState` | an evicted client is `Lost` with the server's reason as status (M7-6, item 13) |

## Tests

`vsrTests "[StudioProtocol]"` (codecs), `"[StudioClient]"` (client core
against a fake server), `"[StudioServer]"` (server against a raw
`NetworkClient`: session, scene edits, and in
`test_StudioServerProjectOps.cpp` the project ops, Remote Browse, Server
Tasks and Data Roots, in `test_StudioServerViewport.cpp` picking, the
viewport passes and the array histogram), `"[StudioRemote]"` (server and client core in one
process) and `"[StudioTestClient]"` (the test client's script runner against
an in-process server). Those that render use `helide` and skip when it
cannot be loaded. The new `ProjectContext` operations are covered by
`"[SciVisStudio]"`.

`"[StudioRemote]"` (`src/tests/test_StudioRemoteE2E.cpp`) also drives the
project layer through the client core against the in-process server:
`NewProject`, an unnamed `CreateShot` landing in the replica, `SetActiveShot`
seen in the frame headers, a `SaveProject` task tracked in `tasks()` to
completion with the replica clean afterwards, `ListDirectory` of the Data
Root marking the saved `ProjectDirectory`, `OpenProject` of it rebuilding
mirror and replica, and a request pending across a server stop failing once
with "connection lost".

The same file drives milestone 6 through the client core: `setPlaying(true)`
on a 12-frame looping shot at 30 fps with consecutive frame headers stepping
by exactly one (or wrapping), `setPlaying(false)` with the replica resting on
the frame the next headers show, a paused `setTime` seen in the header and
committed by exactly one snapshot, a mirror camera edit surviving a `setTime`
on the server, a non-looping shot auto-stopping with one snapshot on its last
frame; and, on an imported triangle, a centre `pick` whose identity resolves
in the mirror and a corner miss, `setOutline` and a `DEPTH`
`setViewportSettings` with frames still arriving, and
`requestArrayHistogram` summing a scalar array's bins to its element count
and refusing the mesh's vector array.

End to end, `ctest -R StudioScenario` runs each scenario script under
[`test_client/scenarios/`](test_client/scenarios) against a freshly launched
`scivisStudioServer`; see [`test_client/README.md`](test_client/README.md)
for the headless test client, its command vocabulary and how to run a
scenario by hand.

### Driving project ops from the test client

Every server op has a `scivisStudioTestClient` command of the same shape
(`create-shot [NAME]`, `import-static-dataset PATH [NAME] [IMPORTER]`,
`save-project [DIR]`, `list-directory PATH`, `cancel-task ID`, ...): the
command mints a request id, sends the request and waits for its reply;
`await-task` waits for a launched task to end, `await-snapshot` for the
Project Snapshot that confirms a mutation, and `assert project.shots == 2`
or `assert shot.$lastShotId.name == Intro` reads the replica. Paths come
from `$dataRoot` (the first root `list-roots` reports) and ids from the
`$last*Id` variables the replies fill. `no-wait` sends without waiting so
several requests can be in flight; `expect-fail` asserts a refused reply.
A session against a running server:

```bash
scivisStudioTestClient --port 12345 \
  -e 'connect; list-roots' \
  -e 'import-static-dataset $dataRoot/mesh.obj Mesh OBJ; await-task; await-snapshot' \
  -e 'assert project.datasets == 1; save-project $dataRoot/demo; await-task' \
  -e 'disconnect'
```

The scenarios under `test_client/scenarios/` (`project_lifecycle`, `rigs`,
`color_maps`, `datasets`, `tasks`, `browse`, `errors_project`, and `all_m5`
for the whole surface in one session) are the worked examples; the test
client README lists every command, variable and assert value.

### Driving playback, picking and the viewport from the test client

Milestone 6 has the same shape: `set-playing SHOT on|off` is a request
command (reply, then `await-snapshot` for the commit); `set-time SHOT FRAME`,
`set-outline [TYPE INDEX|none]` and `viewport-settings KEY=VALUE...` are
one-way latch sends (`viewport-settings` composes edits into the remembered
struct and sends it whole); `pick X Y` waits for its `PickReply` and fills
`$lastPickType`/`$lastPickIndex`; `request-array-histogram TYPE INDEX BINS`
fills the `histogram.*` values; `await-warning` waits for a
`TimeAdvanceWarning`. `await-frame-advance N` consumes frames until `N`
header changes were seen and `frames.maxStep` reports the largest forward
step between consecutive headers (a wrap or a scrub back does not count).

```bash
scivisStudioTestClient --port 12345 \
  -e 'connect; update-shot active frameCount=12 fps=30 loop=on; await-snapshot' \
  -e 'set-encodings raw; set-frame-config 32 24; start-rendering; await-frame' \
  -e 'set-playing active on; await-snapshot; await-frame-advance 5' \
  -e 'assert frames.maxStep <= 1; set-playing active off; await-snapshot' \
  -e 'pick 16 12; set-outline $lastPickType $lastPickIndex; await-frame 2' \
  -e 'viewport-settings visualizeAOV=DEPTH; await-frame 2; stop-rendering; disconnect'
```

The scenarios `playback`, `autostop`, `scrub`, `pick`, `viewport` and
`histogram` under `test_client/scenarios/` cover the surface one behaviour
each; `pick` and `histogram` copy `fixtures/triangle.obj` into the Data Root
first.
