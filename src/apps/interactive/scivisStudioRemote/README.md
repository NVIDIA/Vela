# SciVis Studio Remote

The client-server split of SciVis Studio: `scivisStudioServer` owns the
project, the scene and the ANARI device and streams frames; the thin
`scivisStudioClient` shows them and edits the scene through a Structural
Mirror. Design: [`docs/scivis-studio-client-server.md`](../../../../docs/scivis-studio-client-server.md);
vocabulary: [`CONTEXT.md`](CONTEXT.md).

For a visual code tour, open the
[interactive architecture atlas](../../../../docs/scivis-studio-architecture.html)
in a browser, or read its
[Markdown overview](../../../../docs/scivis-studio-architecture.md).

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
- `--port` (default 12345; 0 asks the OS for a free port, named in the
  `Listening on port N` line).

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
replayed before, `TaskCompleted`/`TaskFailed` verbatim, between the
`FrameConfig` and the `ProjectSnapshot`, then a `TaskProgress{message = description}` for a
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
  an incoming `currentFrame` as it ignores `playing`, and the Timeline's
  patch never carries one anyway.
- **`TimeAdvanceWarning`.** A `FileBinding` that cannot load a frame reports
  `{frame, message}` to its `AnimationManager`, which records it against the
  clock frame being applied (the shot frame the Timeline shows; a binding
  with fewer files than the shot has frames reports its own file index, and
  the manager boundary is the one place that converts) and hands it to its
  `LoadFailureCallback`. The server's `Playback` holds that slot and sends
  one `TimeAdvanceWarning{shotId, frame, message}` per report while a
  session is up. Load failure never stops playback. The monolith sets no
  callback, so a report there is dropped (the binding logs it regardless).
- **The manipulator follows client camera edits.** An edit landing on the
  active shot's camera object updates the server's `m_ctx.view.manipulator`
  (`followCameraEdit`), and a camera rig without keyframes has its `current`
  view follow too. A `SetObjectMetadata` carrying `manipulator.*` is taken
  exactly (`updateManipulatorFromCamera`); a `SetObjectParameter` is taken
  from the camera pose (`updateManipulatorFromCameraPose`), which cannot
  recover an orbit centre from a position/direction/up triple and derives one
  from the manipulator's own distance. The route is the edit's, not the
  camera's: the server's camera always carries the manipulator metadata
  `applyActiveShot()` derived from the rig, and a parameter edit has just
  made it stale. A client orbit sends both, metadata first, so the pose edit
  that follows re-derives the centre the metadata just set (ADR 0036,
  `PROTOCOL_VERSION` 11). So `applyActiveShot()`, which re-samples the camera
  from the rig on every time change, writes the client's own view back
  instead of a stale one, and a scrub or `SetPlaying` never snaps the view; a
  rig with keyframes drives the camera during playback as designed. An
  orthographic shot camera is followed through its `height` and `position`
  too (`Manipulator::setFixedDistancePose`), since `updateCameraObject`
  derives both from the manipulator's distances.

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
  property, read once before every render and used twice: the
  `BoxOutlineRenderPass` draws them when the box is shown, and every frame
  header carries them.
- **Reset View.** The client frames its view with the world bounds off the
  last frame's header and `vsr::rendering::defaultViewForBounds()`, the same
  helper `RenderIndex::computeDefaultView()` uses, so `Center`, `Distance`
  and `Angle + Distance + Center` behave as they do in the monolith. Nothing
  is asked of the server: the new pose reaches it as the camera parameter
  edits any manipulator change makes. Before the first frame the menu items
  do nothing but log -- there is no scene to frame yet -- and during playback
  of an animated scene the bounds are the ones the picture on screen was
  rendered against.
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
8: 2 when `TaskFailed` gained `framesCompleted` (optional on the wire), 3
when `Disconnect` gained a reason, 4 when `UpdateShot` and `ProjectSnapshot`
moved to the model's one Shot and Project serialization, 5 when a
`SceneObjectRef` became one object-reference leaf, the task endings gained a
`results` subtree (`RenderShotResult` replacing `framesCompleted`) and
`ImportSubtreeDataset` split from `ImportStaticDataset`, 6 when `UpdateShot`
became a `ShotPatch` of the fields to change (PR review fix-ups below), 7
when `LoadDatasetArchive` gained the loaded dataset's name, and 8 when the
UI state left the wire (`SaveProject` lost `uiState`; the `UIState` message,
107, is retired and its value not reused).

### Shot rendering

- **A Server Task with a sync prelude.** `RenderShot{shotId}` is refused at
  once when the shot does not exist, when the project is not saved ("project
  is not saved; save it before rendering") or when a render is already queued
  or running ("render in progress"). Otherwise the shot becomes the active
  one; when that is a switch, the loop rebinds the pipeline (which also pins
  the shot's renderer settings to the server's library, as every bind does)
  and a `ProjectSnapshot` follows the `TaskStartedResult` reply, before the
  task's first progress, so the client sees the switch before the first
  frame renders (rendering the shot that is active already has no switch to
  show). The body is `renderActiveShotToFrames` -- the same
  engine path as `scivisStudioRenderShot` and the monolith -- with a per-frame
  hook: `TaskProgress{current = frame, total = frames, message = "frame N of
  M"}` before each frame. `TaskCompleted{message = output directory,
  results = RenderShotResult{framesCompleted}}` ends it; the directory is
  `<project>/renders/<shotId>/`,
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
  `TaskFailed{"cancelled", RenderShotResult{framesCompleted}}`, and the
  `CancelTask` reply is
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
- **UI state stays out of it.** The client owns its layout (see "Layout"
  below), so nothing about windows or docking travels on the wire. A project
  authored by the monolith may still carry a `{windows, layout, settings}`
  node in its manifest, and the server preserves it: it holds the tree the
  open read -- from `--project` at startup (`setupProject`) or from
  `OpenProject` -- and `SaveProject` writes that same tree back, so a save
  from here never drops a layout the monolith wrote. No client ever sees
  it.

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
File Animation) and Add Shot. A save carries the project and nothing else:
the layout is the client's, not the project's (see "Layout" below).

### Time, picking and passes (milestone 6)

The client has no `AnimationManager`. The **Timeline** window
(`client/windows/Timeline.*`) is the monolith's transport row and ruler
without tracks: Play/Pause sends the `SetPlaying` project op and the button
follows the replica alone; Stop is `SetPlaying(false)` then `SetTime 0`;
dragging or clicking the ruler and the frame field send `SetTime`, at most
one per UI frame with the latest value; Loop, Frames and FPS each travel as
an `UpdateShot` patch of that one field. The frame shown is the drag while
scrubbing, the last frame
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
window's own settings (the client's layout file) and are re-sent after every
bootstrap.

The **Histogram** window (`client/windows/HistogramPanel.*`) lists the array
parameters of the first selected object (and of a volume's spatial field),
takes a bin count and asks the server with `RequestArrayHistogram`; the
reply is plotted, a refusal shows the server's error text.

The **TF Editor** window is `vsr_ui_imgui`'s stock `TransferFunctionEditor`,
locked like the other editors and pointed at a `RemoteArrayAccess`
(`client/RemoteArrayAccess.*` over `client/ArrayHydration.*`). It edits the
Transfer Function of whichever volumes are selected: the colour ramp and the
opacity curve travel as `SetArrayData` and `SetObjectMetadata`, the value
range, opacity scale and unit distance as ordinary parameter writes. The
first frame a volume is selected the window shows that it is fetching the
samples, because the mirror holds its colour Array as a proxy; once they
arrive the widget behaves exactly as it does in the monolith.

### Rendering, task records, layout, loss (milestone 7)

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
record is failed with "connection lost", marked `failedByClient`; a
`TaskCompleted`/`TaskFailed` overwrites the record in place, replayed inside
the bracket or not (same label, the server's ending). A `TaskStarted` reply,
and a `TaskProgress` for a record that finished, start the record over
(label, state, progress, outcome and `render`): a restarted server counts
ids from 1 again, so an id that already finished here names a new task, and
the replay's progress for a task this client failed is the same case (the
record takes the replay's description as label). `render` is the one field
a start-over keeps, and only on a record this client failed at
`BootstrapBegin` (`failedByClient`): the server never ended that task, so a
render it named may still be running and still refusing edits, and the
editors must go on saying so. Records the server never mentions again stay
Failed until "Clear finished" -- they were queued tasks the server dropped
with the old session. `ProjectOps::onTaskEnded` fires once per ending that
is news -- the record was unfinished, or the client had failed it itself --
and the Application toasts on it; a replayed ending for a record the server
already ended is not news (the bootstrap replays every ending it has not
replayed before, whether or not this client saw it live), and the client's
own "connection lost" failures are not endings (the banner said it), so the
ending the replay brings for such a task still toasts.

**Layout.** The layout is the client's own, never the project's: which
project is open moves no panel, and connecting, opening or saving never
touches the docking. `Application::saveClientUIState()` writes `{windows,
layout, settings/fontScale}` -- each window's `saveSettings`,
`ImGui::SaveIniSettingsToMemory()` and the *View* menu's font scale -- to
`~/.config/vsr/studioClientUI.vsr`
(`%APPDATA%\vsr\...` on Windows) in `teardown()`, and
`loadClientUIState()` applies it through the base class'
`applyUIStateTree` in `setupWindows()`, after the built-in default layout
that stands when the file is missing (first run) or unreadable.
`--noDefaultLayout` skips the restore too. The client is `fontScale`'s only
writer -- it has no App Settings dialog and never calls "Save as Defaults" --
so the base class' `appSettings.vsr`, applied earlier in `setupWindows()`,
supplies the value on a first run and this file overrides it afterwards;
`applyUIStateTree` only reaches `m_uiConfig`, so the restore ends with
`m_appSettingsDialog->applySettings()` to push the scale into ImGui.
`uiRounding` and the other application settings are not in the file and stay
in `appSettings.vsr`. *View -> Font Scale* (a drag, plus *Reset Font Scale*
for 1.0) changes it live, and *View -> Restore Default Layout* goes back to
the built-in default at any time. That default (`getDefaultLayout()`) is
maintained by hand: arrange the docking, press **F1** to print
`SaveIniSettingsToMemory()` to stdout, and paste the dump over the string.
The client's `uiFrameStart()` replaces the base class' one, so it repeats
that binding rather than inheriting it.

**Loss and reconnect** (`ServerConnection`). A loss keeps the mirror,
replica and last frame as a frozen read-only view; pending requests fail
once with "connection lost"; task records are handled at the next
`BootstrapBegin` as above. Between a reconnect's Hello and its
`BootstrapBegin` the client is `Connected` but not `bootstrapped()` (its
Session Phase is `AwaitingBootstrap`): the replica on screen is the previous
session's, so every editor is read-only (`ServerConnection::canSend`, which
the Object and Database editors read through `EditorContext`) -- a wait that
lasts as long as the render a busy server finishes before it bootstraps
anyone. A loss *during* a bootstrap empties the mirror
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

- **Color map objects** -- *v1 behaviour, and this entry was wrong.* It
  described `ColorMapRecord` and its `<colorMapId>_colormap` Array as the
  mechanism behind transfer-function editing. They are not: the
  TransferFunctionEditor never reads or writes those Arrays, it overwrites
  the selected **Volume's own** `"color"` parameter Array. The two are
  unrelated concepts and stay that way (ADR 0038). What this entry got right
  applies only to the unbound `<colorMapId>_colormap` Arrays: they are
  created with 256 default samples, persisted nowhere, and regenerated by
  `ensureColorMapArrays` on every open. Nothing reads them, so nothing
  notices. `CreateColorMap` / `RenameColorMap` / `RemoveColorMap` remain the
  extent of color-map support, and a Color Map still names a mapping rather
  than supplying one; binding volumes to Color Maps is a separate feature
  that has not been designed.

- **Transfer-function editing** -- *implemented (v12).* A Volume's Transfer
  Function -- its `"color"` samples, its `opacityControlPoints`, and the
  `valueRange` / `opacity` / `unitDistance` parameters beside them -- is what
  the editor edits. The parameter leg always worked on the optimistic path.
  Four things did not, and only two of them had been written down:

  1. *Array samples had no message.* `SetArrayData` re-tags
     `vsr::network::messages::TransferArrayData` with a Studio type value,
     the way `SceneMessages.h` re-tags the scene transfers.
  2. *Array-valued metadata did not ride `SetObjectMetadata`* (ADR 0036
     carried only scalar metadata). It does now; an entry is an array one, a
     value one, or a removal.
  3. *The client had no samples to open an editor on.* The mirror holds
     arrays as proxies, and `map()` on a proxy returns null, so the stock
     widget could not even draw one. `RequestArrayData` fetches an array's
     contents, `ArrayHydration` fills the mirror's copy in place, and only
     then is the array declared editable -- declaring it first would send the
     server its own samples straight back.
  4. *A scalar-color volume needed an Array created and bound*, which a
     client may not do (`CreateObject` and `BindArray` are both refused, and
     identity is server-minted). ADR 0037 removes the case instead of
     transporting it: every transfer-function Volume carries an Array.

  The widget itself is the stock `vsr_ui_imgui` one. Where its samples come
  from is the single thing it asks through a seam
  (`TransferFunctionArrayAccess`); the monolith sets none and reads its
  host-resident arrays directly.

  The client sends array contents only for arrays a panel has hydrated, and
  drops every declaration when the mirror is replaced. The wire would carry
  any array -- the server trusts the client here, as the optimistic parameter
  lane always has -- but the client offers no way to write one nobody opened
  an editor on. Inside an update batch the dirty arrays are flushed once, so
  a knob drag sends one message per array however many times the widget
  rewrites the samples.

  Persistence was never a blocker: a Volume's colour samples and its metadata
  arrays both serialize into its dataset's archive and round-trip. What was
  missing was the dirty mark -- `DatasetDirtyDelegate` had no metadata
  signal, so an opacity-only edit would have been applied, rendered, and
  dropped at save time once the two halves stopped travelling together.
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
  `CloneCameraRig{cameraRigId}`, and an `UpdateCameraRig{cameraRigId,
  patch}` mirroring `UpdateShot`. `UpdateShot`'s patch covers every Shot
  field except `playing`, which stays with `SetPlaying`.
- **Naming a loaded Dataset Archive** -- *fixed* in the PR review fix-ups.
  `LoadDatasetArchive` carried no name, so the Add Static Dataset dialog
  applied a typed name with a follow-up `RenameDataset` once the snapshot
  after the load showed exactly one new dataset id
  (`client/ArchiveRenameFollowUp`). The request now carries `name`
  (`PROTOCOL_VERSION` 7) and the follow-up is gone.
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

- **One description per payload.** Every payload under `protocol/` was
  written twice, a `toNode()` and a `fromNode()` each repeating every wire
  name (87 pairs, 46 by hand and 41 through seven shape macros in
  `PayloadMacros.h`), and two optional-field policies coexisted:
  `readChildOr()` silently defaulted a *mistyped* child while
  `readOptionalChild()` rejected it. A payload now describes its wire shape
  once as a `fields(V &, T &)` template over a visitor (`required`,
  `optional`, `requiredEnum`/`optionalEnum`, `child`/`optionalChild`,
  `list`, `subtree`); `PayloadCommon.h`'s `Writer` and `Reader` walk it and
  generic `toNode()`/`fromNode()` templates, enabled only for types with a
  `fields()`, replace the hand-written pairs and the macros. `fromNode()`
  reads into a fresh `T` and assigns on success, so absent optionals read as
  the struct's defaults and a rejected payload leaves the output untouched.
  The strict policy is the only one now: absent optional means default,
  present but mistyped means reject, absent required means reject;
  `readChildOr()` is gone and the lenient reads (`Hello.buildInfo`,
  `TaskProgress.current/total`, `ProjectOpReply.error`,
  `DirectoryEntry.size/mtimeSeconds`, the `ProjectSnapshot` runtime sidecar,
  ...) now reject a mistyped child. The wire format is unchanged: a hex dump
  of 101 populated fixtures covering every payload matched before and after,
  so no `PROTOCOL_VERSION` bump. Still hand-written: `SetEncodings`,
  `SetObjectParameter` and `ArrayHistogramResult` (custom value shapes),
  `ProjectSnapshot`, and `Shot`/`ShotRenderSettings`/`UpdateShot`, which
  the next fix-up replaces. A nested type outside `namespace protocol` must
  either carry a `fields()` description or have its codec declared where
  `PayloadCommon.h`'s templates can see it: the type's own namespace, which
  ADL reaches, not a `shot::`-style sub-namespace, which it does not.

- **One serializer for Shot, one for Project.** `Shot` had two wire schemas
  in one protocol: `UpdateShot` wrote bindings keyed by dataset id and
  included `camera`; the manifest writer (`projectToNode`, which
  `ProjectSnapshot` reused) wrote bindings as an ordered list and dropped
  `camera`, so a client received a shot in one shape and sent it back in the
  other. `ProjectSnapshot` was itself a second Project serializer: about 190
  lines re-serializing Dataset, LightRig and CameraRig runtime fields into a
  `runtime/` sidecar keyed by id, decoding through the manifest reader and
  then patching the result, on a scratch copy of the whole tree because the
  readers took non-const nodes. Now the model library owns both:
  `toNode(const Shot &, DataNode &, ProjectForm)` / `fromNode` in `Shot.h`
  and `projectToNode` / `nodeToProject` taking `ProjectForm{Manifest, Full}`
  in `ProjectSerialization.h`. `Manifest` is what `project.vsr` stores and is
  unchanged (a golden canonical dump of a fully populated project, taken
  before the change, is a `[SciVisStudio]` scenario); `Full` writes every
  runtime field inline under its entity in one pass. `UpdateShot` became a
  `fields()` description nesting the Shot in `Full` form, so its bindings
  moved to the manifest's ordered list (`PROTOCOL_VERSION` 4), and
  `ProjectSnapshot.cpp` is the two-line `Full` encoding. The typed-field
  primitives the codecs share (`writeChild`/`readChild`, the enum and
  nested-node helpers, the lists, the ANARI type names and the
  `SceneObjectRef`/`SceneNodeRef` codecs) moved down from `PayloadCommon.h`
  to the model's `DataNodeFields.h`, where both layers reach them and ADL
  finds the model's overloads; `PayloadCommon.h` re-exports them. Every
  model reader now takes `const DataNode &`, returns `bool` and assigns its
  output only on success, with the protocol's policy throughout: an entity
  without an id, a mistyped child, an unknown enum spelling
  (`interpolationFromString` is strict and returns `std::optional`) or a
  malformed camera pose is rejected rather than read as a default. That
  reaches the manifest too: a malformed `scivisStudio` section fails the
  open with an error instead of loading defaults, and a malformed Camera Rig
  Archive is skipped like a missing one. The v1-v4 compatibility paths
  (inline dataset metadata with its legacy enum aliases, inline shot camera
  rigs) run only for `Manifest` reads, whose field names they share; `Full`
  reads the runtime fields and skips them. A snapshot with a corrupt camera
  rig (mistyped keyframe frame, unknown easing, mistyped pose) is now
  rejected, covered in `[StudioProtocol]`.

- **Protocol boundary cleanups** (`PROTOCOL_VERSION` 5). Six small
  findings, one of which did not hold up:
  - *`SubtreePtr`* was to become a `DataNode` held by value. A `DataNode`'s
    children live in its `DataTree`'s forest and a detached or copied node
    has no forest (`operator[]` on one dereferences a null ref), so a
    payload cannot hold a subtree by value; `SubtreePtr` stays, written up
    in the handoff's follow-ups. The other five landed.
  - *`SceneObjectRef`* travelled as `{type: "ANARI_CAMERA", objectIndex}`
    with a 4096-entry table in the model to invert `anari::toString`. It is
    now one leaf holding the object reference a `DataNode` already carries
    for an object parameter (`Any(type, index)`); an unset ref is an empty
    leaf both ways (a `Shot` without a camera). `toString(anari::DataType)`,
    `anariTypeFromString` and the table are gone; the test client scans
    `anari::toString` at script-parse time for its type names.
  - *Task endings* carried a RenderShot-only `framesCompleted`. Both now
    carry a `results` subtree like `ProjectOpReply`, decoded with the same
    `results<R>()` (now in `PayloadCommon.h`, generic over any payload with
    a `results`), and `RenderShotResult{framesCompleted}` is what a render's
    ending holds. The client's `TaskRecord` and the test client's record
    keep their `framesCompleted`, decoded from it, so the panel, toasts,
    `task.*` fields and `EVT` lines are unchanged.
    `ImportStaticDataset.fromSubtreeArchive` made `importerType` meaningless
    when set and routed to `addStaticDatasetFromSubtree()`, a Layer Subtree
    Archive import that `LoadDatasetArchive` (a Dataset Archive) does not
    cover, so the bit became its own request, `ImportSubtreeDataset` (63).
  - *`SetFrameConfig`/`FrameConfig`* are one `FrameSize<TAG>` description;
    `FrameHeader` extends the wire prefix `FrameHeaderFixed` (typed enums,
    16 bytes) so the frame codec copies a struct instead of six fields each
    way; the image byte count follows as its own `uint32_t`. Bytes on the
    wire are unchanged.
  - *Thin wrappers*: `encodeSceneMessage<TYPE>()` static-asserts the scene
    type instead of guarding a literal at runtime (`SceneMessages.cpp`
    deleted); `encode<T>()` and `StructuredMessage::toMessage()` share one
    `makeMessage(type, tree)` overload in `vsr/network/Message.hpp`;
    `decode<T>()` no longer accepts a header-only message as an empty tree
    (nothing sends one); `StudioEndpoint.{h,cpp}` folded into
    `StudioProtocol.{h,cpp}`.
  - *Port type*: `uint16_t` end to end (`DEFAULT_PORT`, `parsePort()`,
    `NetworkServer`/`NetworkClient` signatures, `ServerOptions`, the client
    and test client, the test helpers); the `short` sign-hazard comment in
    `NetworkChannel.cpp` is gone with the casts. Shared vsr code touched:
    the `makeMessage()` overload and the `NetworkChannel` port signatures
    (the render server, MPI server and remote viewer keep their `short` and
    convert implicitly).

- **The monolith goes through the Shot ops; color maps and the shot record
  leave ProjectContext.** `ProjectContext` had gained `removeShot`,
  `updateShot`, `setActiveShot`, `setPlaying` and `setActiveShotFrame` as
  validated whole operations for the server while the monolith's editors kept
  mutating the stored `Shot` inline with their own clamps (seven
  `std::max`/`std::clamp` sites in `ShotEditor`, inline writes in
  `Application`, `CameraRigEditor`, `LightRigEditor` and `ProjectWindow`, and
  `syncAnimationManagerToActiveShot` spelling the clamps a third time). The
  monolith now calls the ops: `ShotEditor` edits a per-frame copy of the
  active shot and lands it with one `updateShot()`, Play/Stop is
  `setPlaying()`, the frame input and the keyframe jumps are
  `setActiveShotFrame()`, shot selection is `setActiveShot()`, the rig
  editors' "Use for Active Shot" is `updateShot()`, and the per-tick
  `shot->playing = animMgr.isPlaying()` is gone because the manager's
  callbacks (and `setPlaying` for `stop()`, which fires none) already write
  it. Visible differences, all intended: scrubbing the frame no longer
  dirties the project (transient state, as the ops document), selecting a
  shot does (`activeShot` is persisted), and an edit to a shot naming a rig
  the project lacks is refused until the selector picks an existing one.
  `ProjectContext.cpp` then shed two blocks into the model library in the
  `project::`/`shot::` free-function style: `ColorMaps.{h,cpp}`
  (`color_map::createColorMap`, `removeColorMap`, `resolveColorMapArray`,
  `ensureColorMapArrays`: the record-array pairing and the `"<id>_colormap"`
  name) and `ShotOps.{h,cpp}` (`shot::removeShot`, `updateShot`,
  `resolveShotCamera`: the validation and the record). `ProjectContext`
  keeps forwarders that add the dirty mark, the null-context guard and the
  animation re-sync; the dispatcher and the tests call the forwarders as
  before. The three identical `findDirectChild` copies (ProjectContext,
  ProjectPersistence, ProjectOpenPersistence, plus a fourth in
  `test_SciVisStudio.cpp`) became one, declared in `ProjectPersistence.h`.
  Two `[SciVisStudio]` scenarios cover the free functions directly.
  `ProjectContext.cpp`: 2088 -> 1963 lines, still above `main`'s 1855, the
  document's target; what remains of the growth is the three playback ops,
  the forwarders themselves, the `openStagedProject` split and the
  dataset-candidate path, none of which the document names.

- **One snapshot per revision change, decided in the loop, never in a
  handler.** The server decided "did this mutate?" by hand in about twelve
  places: a `projectChanged` flag threaded through `finish()`, `fail()`,
  `TaskLaunch` and `TaskResult`, three before/after status compares guessing
  whether a failed load, unload or refresh still changed a dataset, the
  `wasPlaying && !playing` test in `tickPlayback`, four `ProjectSnapshot`
  send sites and six hand-set `rebind` booleans. `ProjectContext` now counts
  its own mutations: `revision()` moves once per whole op that writes the
  Project (every create/remove/rename/update, load/unload, the failed ones
  that leave a mark -- an ImportFailed record, a dataset found Unavailable --
  new/open/save project, `setActiveShot` to another shot, `setPlaying`, the
  auto-stop), never on the per-frame playback write (ADR 0035), and
  `activeShotRevision()` moves with it when which shot renders or its record
  changed. Two whole ops outside the context declare themselves with
  `markRevised()`: the render's shot-state guard (the snapshot after a render
  confirms the Project it left) and the loop's Time-at-Rest commit
  (`commitScrubIfQuiet`). The loop applies one rule in
  `followProjectRevisions()` -- rebind when `activeShotRevision()` moved,
  then, with a session up, one snapshot when `revision()` moved since the
  last sent -- after each request dispatched (so every mutation keeps its
  own snapshot, right after its reply, and a RenderShot's prelude is shown
  before the task's first progress), after a task ran and after the
  playback tick; the bootstrap's snapshot records the revision it carried.
  A failed open moves `activeShotRevision()` too: its apply resets the
  scene before it can fail, and the pipeline must not keep handles into the
  scene that was (the rebind the old `runTaskBody` did unconditionally). The flags, the compares, the
  `rebindActiveShot` host hook and three of the four send sites are gone;
  `runOneTask()` just runs the task. Two visible differences, both the rule
  working as intended: a request that changed nothing (SetActiveShot to the
  active shot, SetPlaying to the state the shot is in, RenderShot of the
  active shot, Unload of an unloaded dataset) no longer sends a snapshot, and a task body that throws no longer forces
  one -- whatever it changed before throwing moved the revision. The render
  test's ordering check now exercises a real switch (a second shot active
  when the render is asked) and asserts the no-switch case sends nothing
  before the progress; every other snapshot count in the suites and the
  scenarios is unchanged. The `[SciVisStudio]` suite covers the counters
  directly.

- **An explicit session state; the playback clock in its own file.**
  `StudioServer` was three objects in one: the server proper (device,
  pipeline, frame render), a session whose ten fields `beginSession` and
  `endSession` reset in two hand-kept copies, and a playback clock. The
  clock is `server/Playback.{h,cpp}` now (the tick, the scrub's rest-commit
  window and the load-failure warnings; the loop still follows the revisions
  after the tick, so the snapshot a commit's `markRevised()` asks for is
  sent by the server as before). The per-session fields are one `Session`
  value (serial, encoding, scene-resend flag, the Frame in flight, the
  pending requests, the pending pick), reset whole where a session begins
  or ends, and where a session stands is the enum alone: `Listening`,
  `AwaitingHello`, `Bootstrapping` (the one iteration that sends the
  Bootstrap; it replaces the `m_bootstrapPending` flag that had escaped the
  enum), `Established`, `Shutdown`. Whether the client asked for frames is
  `streaming()`, which used to be `Rendering`, an enum value re-derived from
  a bool at two sites per iteration; it is the second atomic beside the
  state because tests read both from another thread. The latch's five
  session slots (accept, Hello, loss and close request, each tagged with a
  connection serial, plus a reason and a farewell) were a queue flattened
  into latest-wins optionals whose causal order `applyControlState`
  rebuilt with two `lossOfCurrent()` calls; they are one ordered
  `SessionEvent` queue applied by a switch. One rule the fixed order
  carried implicitly is stated now: the transport holds one socket, so a
  close the server asked for is stale once a later connection was accepted
  (the transport closed that socket when it adopted the next one; the
  `task_replay` scenario caught the restart cutting the new client off),
  and the accept that follows ends the session. The test-only
  `idChannelEnabled` atomic mirror, synced at four sites, is gone; tests
  read `viewport().idChannelEnabled()` (the flag itself became atomic in the
  test-fixture round below, since those polls run while frames stream). The
  client-visible handshake is unchanged and every suite
  and scenario passes, with `Connected`/`Rendering` read as
  `Established`/`streaming()`. `StudioServer.cpp` 1393 -> 1327 lines; the
  document's ~800 target waits on 08 (`onMessage`) and 09 (the renderer
  binding).
- **Dispatch and latch cleanups in the server.** `onMessage` latched nine
  inputs through nine copies of decode, refuse-if-malformed, lock, assign;
  three private templates do it once (`decodeOrRefuse<T>`,
  `latch(msg, &ControlState::slot)`, `latchEdit<T>`), `SetEncodings` latches
  the message and the loop negotiates the encoding when it applies it, and
  the hand-enumerated "was anything dropped" expression is
  `ControlState::hasInput()`. `RequestPolicy`'s three bools, of whose eight
  combinations five occurred, are one `RequestKind` (`SyncMutating`,
  `SyncReadOnly`, `Task`, `RenderShot`, `Independent`), one row per
  alternative in the same compile-time-complete table;
  `waitsForQueuedTasks`, `independentOfQueuedTasks` and `mutatesProject`
  read off the kind, and a temporary `static_assert` checked them equal to
  the old three predicates for every alternative before the old table went.
  `TaskResult` said "cancelled" three ways (`ok == false`, `error ==
  "cancelled"`, `cancelled == true`); it carries `TaskOutcome` (`Completed`,
  `Failed`, `Cancelled`) and `taskFailure(error)` builds a failure, so the
  wire string is written once and `cancel()` and `endingOf()` read one
  field. RenderShot's two loop concerns left the dispatcher `Host`:
  `shutdownRequested` is `ServerTaskRunner::stopAll()`, an atomic every
  `TaskControl::cancelRequested()` ORs in, called from `requestShutdown()`;
  `dropLatchedInputs` and the `LatchGuard` inside the render body are gone
  because `runOne()` hands the ending back recorded but unsent and the
  loop's `runOneTask()` discards the latch after an exclusive task, then
  `sendEnding()`s -- the guarantee 53c99ac made ("discard however the body
  leaves") now holds by construction, since the body cannot send its own
  ending. The handlers' resolve-or-fail blocks and ok replies are
  `resolveOrFail(req, path)` and `ok(req[, results])`. `StudioServer.cpp`
  1327 -> 1321 lines: the latch collapse paid for the `runOneTask()` the
  loop gained, and the remaining bulk (`bootstrap`, `followProjectRevisions`,
  the frame path) is the server proper, which no fix-up owns; the ~800 line
  figure was a review estimate, not a requirement.

- **The server reuses the studio core's renderer binding and device
  fallback.** `loadFirstAvailableDevice` was written twice (`StudioServer.cpp`
  and `RenderShot.cpp`, identical but for the log text) and "renderers of the
  library, or create the standard set; the shot's pick if it is one of them,
  else the first; write the pick back" three times (`StudioServer::
  bindActiveShotRendering`, the monolith's `ShotEditor::
  buildUI_rendererSelector`, and a stricter RenderShot that refused a pick
  the loaded device did not have). The fallback is now
  `ANARIDeviceManager::loadFirstAvailableDevice(libName)`, one warning, next
  to the `loadDevice` it wraps; the binding is `ProjectContext::
  bindShotRenderer(shot, library, device) -> RendererAppRef`. The bind
  writes the pick into the `Shot` it is handed and nothing else: the
  monolith's ShotEditor hands it the copy it edits, which lands through
  `updateShot` (whose dirty marking stays the only one on that path), so
  the server alone keeps its
  rule (filling in a shot that never picked leaves the dirty flag alone;
  overriding a real pick is an edit), by comparing the settings around the
  call. RenderShot does not bind: it loads the device through the shared
  fallback and then refuses a pick the loaded device does not have, as it
  did before. Jefferson chose the refusal over rendering with a fallback
  renderer (PR review, 2026-09-04) because a render must never rewrite the
  shot's renderer, and the monolith's render path restores the dirty flag
  around the render, which would have hidden such a rewrite from the user
  until the next save. The document asked
  for the private `ensureRendererDefaults(Shot &)` to be promoted into this
  function; it stays as it was, because it does a different job (it picks a
  default *library name* for a new shot from the device manager's list,
  before any device exists) and the two calls it serves have no device to
  bind with. `bindActiveShotRendering` is camera and pass wiring around one
  call and the server's `m_renderers` is gone; the ShotEditor loads the
  device once per library as before and reports the fix-up as an edit of
  its draft. RenderShot binds before it builds its render index, so a shot
  whose library is not on the machine now renders with the fallback
  device's first renderer (and its record names what rendered, dirty flag
  restored with the rest) instead of failing "Renderer object index N is
  unavailable" after the device fallback had already picked another
  library; this is the one behaviour change of the fix-up.
  `ProjectContext::createLightRig` calls `applyActiveShot()` like
  `cloneLightRig` and `loadLightRigArchive`, so a new rig starts hidden
  wherever it is created; the dispatcher's compensating call after
  `CreateLightRig` is gone. Tests: `[App]` covers the fallback (missing
  library, empty name, nothing loadable), `[SciVisStudio]` covers the four
  bind cases and the hidden new rig.

- **One table for the test client's commands.** `test_client/CommandRunner.cpp`
  was 3167 lines: two string-dispatch chains over 80 handlers (107
  `if (name == "...")` checks), 58 `usageError(command, "<literal>")` calls
  each re-checking its own arity, and the vocabulary retyped in
  `commandHelp()`, the header's prose and the README's three tables with
  nothing checking they agreed. The runner now has one `CommandSpec` table
  (`CommandRunner::commands()`, sorted by name, one `findCommand()` lookup):
  name, usage, `minArgs`/`maxArgs`, a `Kind` (`Session`, `Request`, `Wait`)
  and the handler, which is a member of one of three signatures (taking the
  `Command`, the `Deadline` and the prefixes only as it needs them) or a
  request shape bound to its request type (`idRequest<RemoveShot>(...)`, 25
  rows). `execute()` checks the argument count and the prefix legality
  against the row, so about 40 arity checks are gone from the handlers, and
  `usageError()`, `--help` and the README's command table all print the
  row's usage: `--help` is rendered from the table by kind,
  `scivisStudioTestClient --markdown` prints the README's table, and the
  `[StudioTestClient]` suite checks the table is sorted and consistent and
  that `test_client/README.md` carries the generated table verbatim. The
  prefixes travel as a `Modifiers` value into the handlers instead of the
  `m_expectFail`/`m_noWait` members; a prefix is legal iff the row's kind
  admits it (`no-wait` on a `Request`, `expect-fail` on a `Request` or a
  `Wait`), so `await-task`'s own trailing `expect-fail` spelling is gone --
  scripts write `expect-fail await-task [TASKID]` (ten scenario lines and
  four test scripts changed) -- and `expect-fail await-snapshot`, which was
  silently accepted, is now the FAIL `expect-fail await-task-progress`
  always was. `await-snapshot` and `await-task-progress` are `Session` rows:
  they have no failing outcome to invert. The `Describe` callbacks take the
  runner as a parameter so the table's rows can be built without one. The
  file is split by area, each under 600 lines: `CommandRunner.cpp` (run
  loop, prefixes, the table, the pump), `SessionCommands.cpp`,
  `SceneCommands.cpp`, `RequestCommands.cpp`, `WaitCommands.cpp`,
  `NamedValues.cpp`, and the runner-free `AnyText.{h,cpp}` (the ANARI `Any`
  <-> token codec) and `CommandText.{h,cpp}` (record spellings and the
  shared argument parsers). No wire or record-stream change.
- **Field tables for the replica's records; a typed Event; one snapshot
  cursor; shared text helpers.** `namedValue()` was a 355-line chain of
  `if (name == "...")` and nested `if (field == "...")`, and every Shot and
  Dataset field was spelled three times (`applyShotField`, `dumpProject`,
  `namedValue`) plus `assertNames()` plus the README. `test_client/
  RecordFields.{h,cpp}` now holds one `Field<T>` table per replica record
  (`SHOT_FIELDS`, `DATASET_FIELDS`, `LIGHT_RIG_FIELDS`, `CAMERA_RIG_FIELDS`,
  `COLOR_MAP_FIELDS`, `PROJECT_FIELDS`): name, `get`, `set` (null when
  read-only) and how `dump-project` prints it (`Dump::Plain`, `Quoted`, or
  `Omit` for a value only `assert` reads). `dump-project` iterates the
  table, `assert <collection>.<id>.<field>` looks up `get`, `update-shot`
  looks up `set` (`binding.<datasetId>` stays the one parametric Shot
  field, folded in by `shotFieldText`/`setShotField`), and the field lists
  in the documentation come from the table. What `assert` can name is a
  second table, `CommandRunner::namedValues()` in `NamedValues.cpp`: one
  `ValueSpec` per name or pattern (`shot.<id>.<field>`; the text before
  the first `<` is the prefix it matches, an exact row wins) with its
  summary and resolver; the session-side records (`TASK_FIELDS`,
  `FRAME_FIELDS`, `PICK_FIELDS`, `HISTOGRAM_FIELDS`) are file-local tables
  there. `--help` lists the rows, `scivisStudioTestClient --markdown` prints
  the assert-value table after the command table, and the test client
  README carries both, checked by the `[StudioTestClient]` suite. Since
  the tables are the union of what the three readers used to spell,
  `assert` gained `lightRig.<id>.rootNode`, `cameraRig.<id>.keyframes` and
  `shot.<id>.renderSettings.rendererObjectIndex`. `Event` carries a typed
  identity -- `std::optional<StudioMessageType> type`, `requestId`,
  `taskId` -- so `awaitReply`, `pick` and the session commands match on
  `event.type == ProjectOpReply && event.requestId == id` instead of on the
  name and a scan of the fields for `requestId`'s text; `field(key)` reads
  a value where one is wanted (a Frame's `frame`, an Error's `message`).
  The `size_t m_snapshotMark` assigned from six places is a
  `SnapshotCursor` with `markAt(count)`, `advance()` and `passed(count)`,
  and the session answers `snapshotsAtTaskEnd(taskId)` beside
  `snapshotsAtReply(requestId)`, so the runner reads one concept one way.
  `TestSession.cpp`'s copies of `quotedText`, `numberText`,
  `shortTypeName`, `boolText` and `objectText` are gone in favour of
  `CommandText.h` and `AnyText.h` (the shared quoting helper is
  `quotedText`: an unqualified `quoted(text)` is `std::quoted` by ADL
  wherever `<iomanip>` is reachable); `objectText`'s `ANARI_CAMERA 0`
  spelling becomes `camera:0` in the one FAIL that used it. The record
  stream of `all_m5.studio` is byte-identical before and after up to wire
  timing (temp paths, mtimes, Frame arrival, which poll batch a snapshot
  lands in); every scenario passes unchanged.

- **The scenario runner is the test client.** `test_client/scenarios/
  run_scenario.sh` (254 lines of bash) picked a port ahead of the server's
  bind (and retried when it lost the race), parsed `# runner: fixture` and
  `# runner: kill-restart-after N` hints out of each scenario's comment
  block with `sed`/`grep`, and in kill-restart mode tailed the client's
  stdout counting `^OK ` lines to decide when to SIGKILL the server, so any
  command added before the kill moved `N` and `--keep-going` broke the
  count. The lifecycle is now the client's: `--spawn-server SERVER
  [args...]` (the rest of the command line) makes a temporary working
  directory with the data root and one server log, starts the server with
  `--port 0 --data-root <work>/data` appended, reads the bound port off its
  `Listening on port N` line (no pick, no race; `scivisStudioServer` now
  accepts `--port 0`), runs the script from that directory, stops the
  server (SIGTERM, then SIGKILL) and removes the directory on success or
  keeps it, path and server log on stderr, on failure. `--require-device`
  turns a server that loads no ANARI device into exit 77, ctest's skip, so
  the `vsr::StudioScenario.*` contract (`SKIP_RETURN_CODE 77`, 120 s) is
  unchanged and `add_studio_scenario` invokes the binary directly. The
  hints are commands: `copy-fixture <file>` (relative to the script's
  directory, into the spawned server's data root), `kill-server` (SIGKILL,
  then a replacement started on the same port without waiting) and
  `await-server` (its `Listening` line, the session polling meanwhile), all
  `Session` rows that FAIL without a spawned server; the process wrapper is
  `test_client/ServerProcess.{h,cpp}` (`posix_spawnp`, the log file polled
  for the Listening and no-device lines, `check()`/`awaitListening()`,
  `kill()`, `stop()`), which the `[StudioTestClient]` suite drives with
  `/bin/sh` standing in for the server. `loss.studio` reads `kill-server;
  await-lost; ...; await-server; reconnect` and `loss_during_task.studio`
  kills right after `await-task-progress`, with no count to keep in step;
  the eight fixture scenarios open with `copy-fixture fixtures/triangle.obj`
  where the hint was. Every scenario passes; nothing else changes.
- **One client session phase.** The GUI client answered "may the user edit?"
  four times: `ServerConnection::canEmitEdits` (`Phase::Established &&
  m_bootstrapped && !m_bootstrapping`), `EditorContext::canSend` (`Connected
  && bootstrapped() && !bootstrapping() && project()`),
  `Application::m_panelsReadOnly` (set on Connected and Lost, cleared in
  `onBootstrapComplete` and `enterHomeState`) and
  `StudioViewport::m_serverReady` (set by `onServerReady`, cleared by
  `dropMirrorReferences`); underneath, the connection tracked `Phase` (3)
  x `m_bootstrapping` x `m_bootstrapped` x `m_autoRetryEnabled`. One
  `SessionPhase { Idle, AwaitingHello, AwaitingBootstrap, Bootstrapping,
  Ready }` (CONTEXT.md, "Session Phase") replaces the private `Phase` and
  both bools: `bootstrapping()` and `bootstrapped()` are phase queries,
  `canSend()` is `phase() == Ready && project()` defined once on
  `ServerConnection` (`EditorContext::canSend` stays as a null-guarded
  forwarder because the context is the editors' one seam to the
  application, and the modals hold no connection pointer of their own),
  the mirror's delegate is enabled exactly while `Ready` (`syncDelegate`),
  and `setPhase` logs each transition as `setState` does. The Object and
  Database editors' `LockableWindow` takes the `EditorContext` and reads
  `!canSend()`, so `m_panelsReadOnly` and its four writes are gone; one
  visible consequence is that those two panels are now greyed in the home
  state too (no replica, nothing to edit), where `m_panelsReadOnly` had
  left them enabled over an empty mirror. The viewport asks
  `bootstrapped()` instead of keeping
  `m_serverReady`, which also closes a gap: a `TransferScene` outside a
  bootstrap (the server's scene resend after an open) used to clear
  `m_serverReady` for the rest of the session, silencing outline and
  viewport-settings sends until the next bootstrap. `m_autoRetryEnabled`
  and `m_lostAt` became `std::optional<time_point> m_retryDeadline`,
  present exactly while Lost and auto-retrying (set by `declareLoss` and a
  user `connect()` while Lost, cleared when the window passes, when a retry
  is greeted, and by `dropSession`), so `autoRetrying()` is `Lost &&
  m_retryDeadline`; the design doc's Lost-retries / Disconnected-does-not
  rule is unchanged and the `[StudioClient]`, `[StudioRemote]` and project
  ops suites pass unchanged. A new `[StudioClient]` scenario pins the phase
  through connect, a held reconnect bootstrap (`AwaitingBootstrap` with a
  replica and no edits), loss and `disconnect()`. Vocabulary: the client's
  `AwaitingHello` and `Bootstrapping` mean what the server's `SessionState`
  of the same name means; the client's `Ready` is its side of the server's
  `Established`, sharing a name only where both sides wait for the same
  thing.
- **A shot edit is a patch, not a whole-Shot replace.** `UpdateShot`
  carried the whole `Shot`, so the client kept two three-way merge machines:
  the Shot Editor's `m_draft`/`m_draftBase`/`m_draftStale` with an
  `applyEdits()` that field-diffed every Shot member to rebase the user's
  edits onto the latest replica, and the Timeline's own
  `m_draft`/`m_draftStale`/`syncDraft`/`sendDraft` with its own reasoning
  about which `currentFrame` to send along; every new Shot field had to be
  added to both. `UpdateShot` is now `{requestId, shotId, patch}` where
  `patch` is the model's `ShotPatch` (`Shot.h`): every field
  `std::optional`, `renderSettings` a nested patch of the same shape, and
  `datasetBindings` the bindings to *set* (`shot::setDatasetBinding` per
  entry; nothing removes a binding, so no whole-list replace is needed). The
  server's `ProjectContext::updateShot(id, patch)` (over
  `shot::updateShot`'s patch overload) applies it to the stored Shot and runs
  the one validation the whole-Shot form always ran, so clamps, rig checks
  and the renderer/library check are unchanged; the whole-Shot `updateShot`
  stays for the monolith's editors. Each client control now reads the
  replica's value for the UI frame and commits a patch of its field alone
  (the renderer pick is one patch of its three fields); `applyEdits`, both
  drafts and `Timeline::onProjectReplaced` are gone, and the whole editor
  is still greyed while its one update is pending. A snapshot landing
  mid-edit cannot yank a typed edit because ImGui holds it itself; an
  `InputInt`'s +/- step, though, lands on the click frame and would be
  re-read from the replica before the release commits it, so the integer
  fields (Frames, Frame count, Width, Height, Samples and the Timeline's
  frame counter, whose +/- had the same gap) draw through `ui::IntField`,
  which holds the value in progress only while the item is active. The
  model spells each patch's scalar fields once (`forEachField` in
  `Shot.cpp`) and derives both codecs, the emptiness test and `applyPatch`
  from it. The patch design was preferred over field-scoped ops
  (`SetShotPlayback`, `SetShotRigs`, ...) because one message with optional
  fields is what the test client's `f=v` syntax already spelled and what a
  three-field renderer pick needs; the message keeps its name and type (38)
  since the op is still "update this shot". The test client's `update-shot`
  writes each `f=v` into the patch (`Field<Shot, ShotPatch>` rows) and sends
  only those; `playing` stays refused by name. `PROTOCOL_VERSION` 6. The
  two-window case the review asked about (an FPS edit in the Shot Editor
  while the Timeline scrubs) is settled by construction: the FPS patch
  carries no `currentFrame`, the server's Shot already holds the scrubbed
  frame (`setActiveShotFrame` lands it there before the debounced snapshot),
  and the `[StudioServer]` UpdateShot scenario checks that fields a patch
  does not name stand.

- **Pass-throughs and event bookkeeping in the GUI client.** Five findings
  in `client/`, each fixed on the branch:
  - *The model's finders, called directly.* `ReplicaView` re-exported
    `project::findDataset/findShot/activeShot/findColorMap`,
    `light_rig::findLightRig` and `camera_rig::findCameraRig` as one-line
    forwards and wrapped `dataset::displayStatus`/`toString` the same way.
    The editors and tests call the model. What it genuinely added -- the rig
    use counts, the entity labels ("<none>", "<missing: id>"), the
    project-directory text and the name-sorted views -- lives in the model
    library under `project::`, where the monolith reaches it too:
    `ProjectContext::shotUseCount`/`cameraRigUseCount` were the same
    `count_if` and are gone. `ReplicaView.{h,cpp}` are deleted; their
    scenario is a `[SciVisStudio]` test of the model.
  - *One confirmation modal.* `Application::uiConfirmation` hand-drew the
    dirty-project question beside the `ui::confirmModal` every editor uses;
    it now opens the popup and calls `ui::confirmModal`, and `Confirmation`
    is `{message, onConfirm}` in an optional engaged while it shows.
  - *A task's ending is an event.* `TaskRecord` carried `generation` and
    `announced`, and `Application::watchTasks` rebuilt a `FlatMap<taskId,
    {generation, state}>` every UI frame to toast each ending once.
    `ProjectOps::onTaskEnded` fires from `handleTaskCompleted/Failed` for
    every ending that is news (above); `failUnfinishedTasks` does not fire.
    `generation`, `announced`, `m_announcedTasks` and `watchTasks` are gone.
    What `announced` also recorded -- that the client, not the server,
    failed the record -- is still what 17e6178's rule needs to keep `render`
    across a start-over, so it stays as `TaskRecord::failedByClient`; the
    alternative, dropping that rule, is Jefferson's call
    (`followups/15-client-cleanup-decisions.md`).
  - *A request is built where it is sent.* `ProjectOps` carried 44 typed
    wrappers (about 530 lines) whose one contribution was the task label;
    `declareFileAnimationDataset` had no caller. The two generic templates
    and `pick()` remain; a caller fills the protocol's request struct and
    hands it to `send()` or `sendForResult<R>()`, and a `taskLabel(const Req
    &)` overload set over the eleven task-launching requests names the
    record (the strings are unchanged). The document asked for designated
    initialisers at the call sites; the project is C++17 with
    `CMAKE_CXX_EXTENSIONS OFF`, so the fields are assigned instead (same
    follow-up). `EditorWindow`'s six one-line forwards to `EditorContext`
    and `EditorContext::Actions` (four `std::function<void()>` read only by
    `ProjectWindow`, which now takes the client `Application` and calls its
    public project actions) are gone.
  - *One op at a time, said once.* Twenty-two `RequestHandle` members, each
    paired with a `BeginDisabled(pending(m_x))` and an assignment, are
    `InFlight`: the handle, `busy(ops)` while the reply is outstanding, and
    `send()`/`sendForResult()` that refuse while busy. The viewport's pick
    keeps a bare handle: a pick is latest-wins (`forget`, then pick again),
    not one-at-a-time. Manual check of the toasts (start a render, cancel it,
    start over) is recorded in the follow-up; the client suite asserts one
    `onTaskEnded` per ending, replayed repeats included.

- **Protocol fields instead of client heuristics.** Three places where a
  guess or a poll stood in for a field or a callback:
  - *A loaded archive's name rides the request.* `LoadDatasetArchive` gains
    `name` (optional on the wire; empty keeps the archive's own name,
    de-duplicated as before), which `ProjectContext::loadDatasetArchive`
    passes to the same `loadDatasetArchiveImpl` that
    `incorporateDatasetCandidate` already named its dataset through -- so a
    typed name is validated (format, uniqueness) before the archive is read
    and the task fails without touching the project, rather than renamed
    after the load. `client/ArchiveRenameFollowUp.{h,cpp}`, the dialog's
    `onProjectReplaced` hook and their `[StudioClient]` scenario are gone;
    the `[StudioServer]` task scenario loads an archive under a requested
    name (and refuses a taken one), and `datasets.studio` does the same
    through `load-dataset-archive <file> [name]`. `PROTOCOL_VERSION` 7.
  - *The server watches an Unloaded dataset's asset.* The client's Dataset
    Editor sent `RefreshDatasetAvailability` once a second for the selected
    Unloaded dataset (`m_availabilityDataset`, `m_lastAvailabilityCheck`,
    an `InFlight`). The server owns the filesystem: the loop now runs
    `ProjectContext::refreshAllUnloadedDatasetAvailability()` (one `exists()`
    per Unloaded dataset) once a second while a session is up, and a
    dataset found missing moves the revision, so the usual
    `followProjectRevisions` snapshot carries the change to every client
    with no poll. The periodic check was chosen over stat-on-snapshot so an
    asset deleted while nothing else changes is still noticed, as the
    monolith's once-a-second check noticed it. The request type stays: the
    test client's `refresh-dataset-availability` forces the check without
    waiting for the tick (`datasets.studio`, `errors_project.studio`); the
    GUI client no longer sends it. The `[StudioServer]` task scenario
    removes an unloaded dataset's asset and waits for the unasked snapshot.
  - *The transport knows when its queue drains.* `NetworkServer` polled its
    own write queue every 5 ms (`replace_when_drained`, `writes_idle()`,
    `REPLACE_DRAIN_POLL`) to learn when a replaced connection's farewell had
    left. `start_next_write` already sees the queue empty; it now runs the
    one-shot continuation `when_writes_idle()` armed there, and the 200 ms
    `m_replaceTimer` stays only as the deadline fallback -- whichever fires
    first adopts the replacement (`adopt_replacement`) and disarms the
    other; a queue failed with its socket counts as drained too, so a
    farewell to a client already gone does not wait out the deadline. `writes_idle()`, the poll constant and the `mutable` on
    `m_writeMutex` are gone. The two generation counters that guarded the
    same hazard (`m_socketGeneration`, IO thread only, and the client's
    atomic `m_connectGeneration`) are one atomic `m_socketGeneration`, bumped
    by `notify_connected()` and by the client's `connect()`/`disconnect()`.
    The `stop()` cleanup the document listed for removal stays: it drops the
    replacement socket and re-arms the accept after a stop inside the drain
    window, which the continuation does not make redundant
    (`followups/16-network-stop-cleanup.md`). `[Network]` passes unchanged,
    the M7 farewell-before-close scenario included.
- **Remote-only state out of the shared vsr code.** Four places where the
  remote feature bolted state or flags onto shared code instead of using
  the seam that existed. *`AnimationManager`'s load-failure mailbox* (a
  256-entry `std::vector<LoadFailure>`, `MAX_LOAD_FAILURES`, an overflow
  latch and a "collected by nobody" warning, drained only by the server) is
  a third push callback beside `TimeChanged` and `PlaybackStopped`:
  `setLoadFailureCallback(int clockFrame, std::string message)`. The
  manager still converts a binding's file index to the clock frame at its
  boundary; `Playback` takes the slot in its constructor, gives it back in
  its destructor, and its `onLoadFailure` sends the `TimeAdvanceWarning`
  while a session is up (the flag `applyTime`/`tick` last saw, so a
  `SetTime` sharing a batch with `Hello` still seeks silently). The
  monolith sets no callback and a report there is dropped (the binding logs
  it regardless). *`LayerTree`'s two flags* (`m_enableAddRemove`, four
  checks; the remote `m_readOnly`, 22) are one `enum class EditMode { Full,
  NoLayerAddRemove, ReadOnly }`; the context menu's mutating items come
  from `buildUI_mutatingMenuItems()`, which `ReadOnly` does not call, the
  clipboard shortcuts from `buildUI_clipboardShortcuts()` likewise, and the
  belt-and-braces inner checks inside disabled scopes are gone. One visible
  change: "delete selected" sits after "load VSR Archive" and before the
  save/export items now rather than last. *`saveUIStateTree`* moved from the
  monolith to `vsr::ui::imgui::Application`, beside `applyUIStateTree`, and
  `saveApplicationState` calls it (a state file's windows/layout/settings
  children now precede the Application Dump's rather than bracket it).
  *The three child names* are `UI_STATE_WINDOWS/LAYOUT/SETTINGS` in
  `vsr/app/UIStateTree.h` -- in `vsr_app` rather than beside
  `applyUIStateTree` as the review suggested, because the model library
  the manifest reader and writer live in links no UI library
  (`followups/17-ui-state-header-home.md`). `[AnimationManager]` observes
  the callback; `[StudioServer]`'s warning scenario and `[SciVisStudio]`'s
  UI-state persistence scenarios pass unchanged.
- **Three boundaries made explicit; one outcome an enum.**
  *`deserialize_Layer`* appended densely, after a warning, when a node
  recorded with `LayerNodeNumbering::Preserved` found its slot taken. For
  the Scene Archive that was harmless; on the wire (`TransferLayer`,
  `TransferScene`, its only `Preserved` callers) a renumbered node silently
  broke every later protocol node reference while the contract says "same
  index on both sides". It returns `bool` now and refuses the whole layer,
  leaving it empty, when a slot cannot be honoured (two nodes recording one
  index, or a dense node having taken it); `deserialize_SceneArchive`
  carries the refusal, `StructuredMessage::execute()` returns `bool` so the
  seven messages can say whether they were applied, and the client and the
  test client answer a scene push the mirror could not take through their
  existing `replyError` (the test client also stamps the event
  `malformed=true`). No wire change, so no `PROTOCOL_VERSION` bump.
  *`DataReader::bytesRemaining`* returned `SIZE_MAX` for "unknown", and
  `FileReader` read an empty file as unsizeable (`m_fileSize == 0` meant
  both); it returns `std::optional<size_t>`, nullopt only for a source that
  cannot be sized, and `DataNode::read` trusts an unsized reader to the
  short read as before. *`AnariSceneRenderPass::render`*'s synchronous
  branch re-implemented render, wait, copy and composite beside the
  asynchronous tail; one tail now, the mode deciding only whether this call
  renders and waits before the common copy or starts the next frame after
  it. Against `main`, synchronous mode composites this call's own render and
  leaves no render in flight after composite (`main` composited the previous
  call's frame and started another); `vsrRender`, `vsrOffline`,
  `renderAnimationSequence`, `RenderShot` and the server's frame loop are
  the callers, and the branch already behaved so before this round. The
  first-frame fill, dead on `main` and here because `startFirstFrame`
  cleared the flag it tested, now covers the first asynchronous call.
  *`RenderShotResult`* carried `completed`/`cancelled`/`error`, eight
  states for three, and `scivisStudioRenderShot` consulted its SIGINT flag
  to tell them apart; it carries `enum class Outcome { Completed, Cancelled,
  Failed }`, the CLI switches on it ("Done", "Canceled after N of M frames",
  "Render failed: <why>"), the server's task maps it to `TaskOutcome`, and
  the header says `ShotStateGuard` logs a restore that fails rather than
  throwing over it. The protocol's `RenderShotResult` (frames completed) is
  a distinct type and untouched. `[ComponentSerialization]` gains the slot
  collision, `[DataTree]` the three readers' answers; on a saved shot the
  CLI's complete, Ctrl-C and refused-renderer runs each report their outcome
  and exit code.
- **One fixture each for the test server, the client core, the scratch
  directory and the payload round trip; no blind sleeps.** Five suites each
  spelled out the same server start (`port = 0`, `library = "helide"`,
  `start`, a `ServerLoop`, connect, Hello, BootstrapEnd, Established) with
  their own `request<R>()`, `nextRequestId`, `TaskEnd`, `waitForTaskEnd` and
  `startedTaskId`, and had drifted (`RenderSession` passed a 30 s timeout,
  the rest the default). `StudioServerTestHelpers.h` holds them once:
  `testServerOptions()`, `RunningServer` (start + loop, never `REQUIRE`s so
  a restart may build one off the test thread; `finished()` says whether
  `run()` returned) and `ServerSession` (a `RunningServer` with one
  bootstrapped `TestClient`, `request<R>()`, `waitForTaskEnd`,
  `waitForSnapshots`, `latestSnapshot`, one timeout for every wait; the
  bootstrap's messages stay recorded until the caller clears them).
  `TaskEnd` gained `framesCompleted` and the end-of-task wait,
  `indexOfReply` and `indexOfCompletedFrom` sit on `TestClient`. The
  per-file structs keep only what is theirs; `ViewportSession` seeds its
  histogram arrays in `beforeLoop` and finds them by name once Established
  and not streaming (the `appContext()` read rule). The remote helpers'
  `RunningServer`, which added a transform node no suite but one test-client
  scenario addressed, became `tempRootServerOptions()` plus that scenario's
  own `beforeLoop`. The two fake-server fixtures and the end-to-end `Client`
  derive from one `MirroredClient` (mirror, `ServerConnection`, bootstrap
  count, error list, `connect(port)`, `waitConnectedAndBootstrapped`).
  Every scratch directory is a `ScopedFixtureDirectory` (`TestDirectories.h`,
  hoisted from the USD fixtures: `std::random_device` name, retried until
  `create_directory` says this process made it, removed by the destructor),
  which also retires the fixed `vsr_studio_data_roots_test` name and the
  `ServerProcess` scenario's `remove_all` a failed `REQUIRE` skipped;
  `writeTriangleObj()` spells the one-triangle OBJ once. The protocol
  suites' two `roundTrip<T>` and their tree-level siblings, plus thirty
  inline `decode<T>(encode(x))` pairs, go through
  `StudioProtocolTestHelpers.h` (`roundTrip`, `roundTripTree(payload,
  into)`); payload structs have no `operator==`, so the field-by-field
  assertions stay. Twelve negative assertions that slept a fixed span and
  read a counter once use `staysFalse(changed, span)` (checks the whole
  span, fails at the first change; spans unchanged, named `ERROR_BURST` and
  `PAST_SCRUB_COMMIT` after the server's 250 ms scrub window); the
  test-client restarter's 500 ms head start, which no predicate on the
  session can express, is `settle()` with the reason beside it. The six
  `waitFor(viewport().idChannelEnabled())` polls in the viewport suite run
  while frames stream, which the `appContext()` rule forbids for a plain
  `bool`; of the two options 07's review left open (restructure the polls
  around `StopRendering`, or one atomic inside `ViewportPasses`)
  `m_idChannelEnabled` is `std::atomic<bool>`, two lines of server code, so
  the suite's assertions keep their meaning
  (`followups/19-fixture-decisions.md`). Every `test_Studio*` suite,
  `[Network]` and all 25 scenarios pass with the same names and counts.
- **One fake Studio server; the test-client suite in four files; flat
  end-to-end scenarios.** `test_StudioTestClient.cpp` (2624 lines) carried
  two more fakes: `ScriptedServer` re-implemented `FakeStudioServer`
  behaviour by behaviour, and `ProjectOpsServer` repeated its channel
  construction and Hello/Ping handling. `FakeStudioServer` gained
  `farewell(msg, closeDelay)` (flush, then close off the IO thread, as the
  server refuses a Hello or evicts a session), an `onHello` hook that runs
  once the built-in bootstrap went out or was held, and `dropConnection()`;
  `ScriptedServer` is gone and the project fake is `FakeProjectServer` in
  `StudioFakeProjectServer.h`, owning a `FakeStudioServer` it installs
  itself on as `onRequest`. The suite is `test_StudioTestClientScript.cpp`
  (parser, options, command table, the `ServerProcess` scenario),
  `test_StudioTestClientConnection.cpp` (failure paths over the fake),
  `test_StudioTestClientProjectOps.cpp` (the project fake) and
  `test_StudioTestClientServer.cpp` (the milestone-3 surface against a
  `RunningServer`), over `StudioTestClientTestHelpers.h` (`runScript`, the
  record-line helpers, `keepGoing()`); same tag, same eleven test cases.
  The project fake keeps only what a real server cannot be made to produce
  (a lagging snapshot, a stray reply, a warning for every scrub, a render
  that holds and refuses an edit until cancelled, a reused task id, a link
  dropped mid-task, the runner's own FAILs); the milestone-6/7 happy path it
  re-ran with scripted replies is owned by the `StudioScenario` scripts
  against a real server (`followups/20-test-client-suite-decisions.md`
  lists the drops and the record-text checks that went with them). At
  Jefferson's direction the four wire and value checks a script cannot make
  stayed on the fake, in one WHEN: the served `request-array-histogram`
  (the only success path of the `histogram.*` values), `viewport-settings`
  composition and its bare re-send, the `set-outline` spellings and their
  identity encodings, and `set-ui-state` composition into the saved tree.
  The five
  `[StudioRemote]` scenarios, each a `WHEN -> THEN -> AND_THEN x 4-6` chain
  with one leaf, are one linear `THEN` each with `INFO` step markers and
  shared step helpers (`waitForLost`, `requestOk`, `completeTask`,
  `openProject`); the first booted two servers for its sibling `THEN` and
  boots one. Of the test files the branch adds, `test_StudioRemoteE2E.cpp`
  (1453, accepted), `test_StudioClientProjectOps.cpp` (1859) and
  `test_StudioServerProjectOps.cpp` (1567) still exceed 1000 lines; the
  last two were outside this round's items.

- **One client session, recorded instead of rewritten.**
  `test_client/TestSession.cpp` (1196 lines) and
  `client/ServerConnection.cpp` (840) were written from one template: the
  same four-step `poll()`, the same `handleMessage` switch, and duplicate
  `beginAttempt`, `closeChannel`, `onInbound`, `onChannelClosed`,
  `markTraffic`, `checkSendFailures`, `handleHello`, `applySceneMessage` and
  `clearMirror`. Two copies of one design gave no independent coverage and
  taxed every protocol change -- Shutdown, UIState and the task replay each
  landed twice in milestone 7. At Jefferson's direction (option 1 of
  `21-decision-test-session-over-server-connection.md`) `TestSession` now
  owns a `client::ServerConnection` and records it: the connection carries
  the socket, the handshake, the Bootstrap, liveness, loss, the mirror, the
  replica and Project Ops, and the session keeps only what a script needs --
  the `Event` queue, the counters, and the replies, picks and task records by
  id. `ServerConnection` gained the three things sharing needed: an
  `onMessage` hook that fires from `poll()` for each message *after* it was
  handled (so an event reports the mirror, replica and phase the message left
  behind, which is what the scene-transfer records print), a `Closing` phase
  with `sendShutdown()`, in which a Shutdown's own close is the completed
  intention and not a loss, and `lastFailure()`, the reason a banner's
  `statusText()` dresses. Two behaviours converged on the test client's,
  which were the better ones: an `Error` before `BootstrapEnd` is the server
  refusing the attempt, and `clearMirror()` drops layers as well as objects
  (so a dropped or lost session leaves the GUI's mirror with no layers
  either -- a visible post-disconnect change, ratified). A refusal after the
  Hellos matched is the server's deliberate answer and retrying would only be
  refused again, so an `Error` in `AwaitingBootstrap` or `Bootstrapping` calls
  `dropSession("server refused: ...")`: the state goes `Disconnected`, the
  banner names the reason and nothing auto-retries. (Calling `attemptFailed`
  there left the GUI at `Connected` over a closed socket, since the state has
  been `Connected` since the Hello. An `Error` in `AwaitingHello`, before any
  session, is still a failed attempt and retries.)
  `autoRetryFor = 0` turns the automatic reconnect off, which is how a
  script's `reconnect` stays the only one.
  Every `StudioScenario` passes and every record stream is unchanged: the 25
  scenarios' streams were captured before and after and differ only in how
  many `Frame` and `TaskProgress` records the run happened to see (and, in
  `render_cancel`, in how many frames the cancelled render had written).
  Requests now go out through the connection's `ProjectOps`
  (`TestSession::sendRequest`, `sendPick`), which mints the request ids the
  session used to mint itself. A scene message the mirror refused still gets
  `malformed=true` on its event, as follow-up 18 promised: the connection
  counts its refusals (`sceneRefusals()`) and the recorder re-derives the
  stamp from a count that moved across the message.
  `TestSession.cpp` is 949 lines of recording, from 1196 of protocol.

- **One set of modals for both Studios.** The three modals the two apps shared
  by copy -- Add Static Dataset, Add File Animation Dataset, Project Location
  -- were about half identical line for line (82 of 161/205, 181 of 362/318,
  31 of 115/112): the same form against two seams. At Jefferson's direction
  (option 2 of `22-decision-loopback-and-shared-editors.md`; option 1, the
  in-process loopback, is not in this PR) there is now one implementation of
  each in `scivisStudio/modals/`, built as `vsr_scivis_studio_modals` -- a
  second OBJECT library beside `vsr_scivis_studio_model` in the same
  directory, rather than sources of it, because the model library must not
  pull in ImGui -- and linked by both apps. The document named one seam, the
  browse; the code had two, so there are two. A `BrowseProvider` says where a
  path comes from -- `NativeBrowseProvider` (the SDL dialogs, its own OS
  window, so `visible()` is false and `renderUI()` only picks up what the
  callback left) or the client's `RemoteBrowseProvider` (Remote Browse, nested
  in the owner's popup, which is why the owner's Escape waits for it). A
  `ModalAction` says what an accepted dialog does and answers
  `ActionResult(ok, error)` once, on the submitting frame or a later one, so
  the dialog greys itself and holds the host's error either way:
  `LocalProjectActions` runs the import behind the Application's task modal
  and checks a directory against the local filesystem; `RemoteProjectActions`
  sends the Project Op and answers from the reply. The footer both seams meet
  in -- the busy line, the error, the browse, Cancel beside the action button
  -- is `modalFooter`, drawn once for all three. The greying is the client's
  alone: `ModalAction::busy()` is false by default and no `Local*Action`
  overrides it, because a host that answers in place is never waiting, so in
  the monolith the busy line never draws and nothing is ever disabled. What
  the monolith did gain is the continuously-updated mixed-extension warning
  and in-dialog errors wherever the host can answer before acting (frames it
  cannot find, a directory it refuses); the static import still runs behind
  the task modal, so its failures still only reach the log. The red rows for
  missing frames are shared, not lost: a host that can tell names them
  (`Action::unreadableFrames`, which the monolith answers by stat'ing and the
  client leaves empty, its frames being the server's), the dialog marks those
  rows red and submits nothing until they are gone, and `errorText` /
  `warningText` wrap as they always claimed to. The browse vocabulary
  (`BrowseMode`, `BrowseRequest`) and the two message colours moved with them,
  and the client's `ui::` namespace re-exports what its editors already used.
  Manual check on 2026-09-08, both apps against the same `mesh.obj`: the
  monolith's dialog imported it as `tri2 [Loaded]`, and the client's -- with
  the path picked through Remote Browse and again typed -- as `tri2-obj
  [Loaded]`. Add Static Dataset clears Name and Source Path on Cancel and
  Escape as well as on accept in both apps now, the client's behaviour, which
  someone adding five datasets from one directory will notice. `test_SciVisStudio`,
  `test_StudioClientProjectOps` and every scenario pass unchanged.

- **The test client takes its spawned server with it.** `run_scenario.sh` had
  `trap cleanup EXIT INT TERM`; `scivisStudioTestClient` had only
  `~ServerProcess()`, which runs on a normal return from `main` alone, so a
  client that was SIGTERMed or SIGINTed on its own, or that died from an
  uncaught exception, left the server it spawned re-parented to init and
  still listening. `stopSpawnedServerOnDeath()` (`test_client/ServerProcess.h`),
  called by `main` once a server is spawned, installs SIGTERM and SIGINT
  handlers that SIGTERM the running child and then re-raise the signal with
  the default disposition, and a `std::set_terminate` hook that SIGTERMs it
  for an uncaught exception. The child's pid is published in a
  `sig_atomic_t` by `start()` and cleared by `reap()`, since a handler cannot
  walk a `ServerProcess`; the handler uses only `kill()`, `signal()` and
  `raise()`. Only those two signals are hooked, so anything else -- SIGSEGV
  and SIGKILL among them -- still orphans the server: Jefferson judged the
  parent-death pipe that would cover every client death heavier than the
  problem (2026-09-08). `--port 0` is ratified as a real server option in the same
  round; the `Listening on port N` line it is read back from is now asserted
  against a live `StudioServer` by `[StudioServer]` "StudioServer's Listening
  line names the port it bound", so the launcher contract cannot drift
  silently, and the `--port` entries in `docs/scivis-studio-client-server.md`
  and the server option list above name the 0 case.

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
| Project: `NewProject`, `OpenProject`, `SaveProject(dir?)` | implemented | `server/ProjectOpDispatcher.cpp`, `server/ProjectOpDispatcherTasks.cpp` | the UI state left the wire (`PROTOCOL_VERSION` 8): the client owns its layout and the server preserves a manifest's node across saves |
| Dataset ops (imports, declare, reimport, rename/remove/unload/refresh, load, archive save/load, incorporate, discover) | implemented | `ProjectOpDispatcher.cpp` (sync), `ProjectOpDispatcherTasks.cpp` (tasks), types 23..35 | task/sync split as listed |
| Shot: `CreateShot`, `RemoveShot`, `UpdateShot`, `SetActiveShot` | implemented | types 36..39 | `UpdateShot` carries a `ShotPatch` (v6) and has no `playing` field |
| Rig: light rig create/clone/remove/rename, add/remove light, camera rig create/remove/rename, archives | implemented | types 40..52 | |
| Color map: `CreateColorMap` both halves atomically, `RenameColorMap`, `RemoveColorMap`; values optimistic parameter edits | **deviates** | types 53..55, `ColorMapCreatedResult` | the id/name ops are implemented; "values" presumed a binding between Color Maps and volumes that does not exist, so there are no values to edit -- a volume's mapping is its Transfer Function instead (ADR 0038) |
| Remote Browse: `ListRoots`, `ListDirectory` | implemented | `server/RemoteBrowse.cpp` | |
| Server Task family: task-id reply, `TaskProgress`, `TaskCompleted`/`TaskFailed`, `CancelTask` | implemented | `server/ServerTaskRunner.cpp`, `protocol/TaskMessages.h` | `TaskFailed.framesCompleted` added (v2), then both endings gained a `results` subtree carrying `RenderShotResult{framesCompleted}` (v5); the bootstrap replays endings since the previous bootstrap, not only the running task (M7-4; spec paragraph tightened) |
| Playback: `SetPlaying`, `SetTime`, `TimeAdvanceWarning{frame, message}` | implemented | `protocol/PlaybackMessages.h` | the warning also names the `shotId` |
| Scene client-to-server: `SetObjectParameter`, `RemoveObjectParameter`, `SetObjectMetadata`, `SetNodeTransform` | implemented | `protocol/SceneEditMessages.h` | `SetObjectMetadata` (146) carries a batch of keys and skips array-valued ones (`PROTOCOL_VERSION` 11, ADR 0036) |
| Scene server-to-client: `TransferScene`, `TransferLayer`, object added/removed, `ProjectSnapshot` | implemented | `protocol/SceneMessages.h`, `ProjectSnapshot.h` | |
| Viewport: `Pick`, `SetOutline`, `ViewportSettings` | implemented | `protocol/ViewportMessages.h` | |
| On-demand: `RequestArrayHistogram` | implemented | `server/ArrayHistogram.cpp` | |
| On-demand: `RequestArrayData` | implemented | `server/ProjectOpDispatcher.cpp` | v12; answers with `ArrayDataResult` so a client can hydrate a mirror proxy it means to edit |
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
| Every frame carries a header (size, format, encoding, shotId, frame, render times, world bounds) | implemented | `FrameMessages.h` | `pipelineMs` (pass-chain wall time) and `renderMs` (ANARI `duration`), shown in the client's viewport overlay; `worldBounds` is what Reset View frames |
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
| Load failure keeps playing, `TimeAdvanceWarning` | implemented | `Playback::onLoadFailure` | |
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
| Outputs `<project>/renders/<shotId>/<prefix>_%04d.png`; partial frames kept; frame count in the ending; preconditions as task failure | implemented | `RenderShot.cpp`, `RenderShotResult` | |
| `scivisStudioRenderShot` unchanged, shares the engine path | implemented | `scivisStudio/RenderShot.cpp` | result struct extended, CLI behaviour kept |
| **Client behavior on server loss** | | | |
| Client declares loss: socket error, or Ping after ~5 s quiet and ~15 s silence | implemented | `client/ServerConnection.cpp` | `ConnectionTimings` defaults 5 s / 15 s |
| Single IO-thread-safe disconnect hook, UI thread polls | implemented | `ServerConnection::onChannelClosed`/`poll` | |
| Freeze in place under a banner | implemented | `client/Application.cpp`, `client/StatusOverlay.cpp` | mirror, replica and last frame kept; a loss during a bootstrap empties the mirror (M7-6, item 8) |
| Auto-retry with backoff for ~a minute, then manual | implemented | `ServerConnection::poll` | a retry greeted by another protocol version ends `Disconnected` with the mismatch text and no retry offer (M7-6, item 12) |
| Reconnect does nothing special; a restarted server is a first connect | implemented | bootstrap | task records are failed at `BootstrapBegin` and revived by the replay (M7-4); the layout is the client's own and no bootstrap touches it |
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
process) and `"[StudioTestClient]"` (the test client's script runner: its
parser and options in `test_StudioTestClientScript.cpp`, its failure paths
over `FakeStudioServer` in `test_StudioTestClientConnection.cpp`, the paths
only `FakeProjectServer` can produce in
`test_StudioTestClientProjectOps.cpp`, and the milestone-3 surface against
an in-process server in `test_StudioTestClientServer.cpp`). Those that
render use `helide` and skip when it cannot be loaded. The new `ProjectContext` operations are covered by
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

The suites share their fixtures: `NetworkTestHelpers.h` (`waitFor`,
`pollUntil`, `staysFalse`, `LOOPBACK`), `TestDirectories.h`
(`ScopedFixtureDirectory`), `StudioServerTestHelpers.h` (`TestClient`,
`RunningServer`, `ServerSession`), `StudioRemoteTestHelpers.h`
(`helideAvailable`, `fastTimings`, `MirroredClient`), `StudioFakeServer.h`
(`FakeStudioServer`: bootstrap, `farewell`, `onHello`, `onRequest`),
`StudioFakeProjectServer.h` (`FakeProjectServer`),
`StudioTestClientTestHelpers.h` (`runScript`, the record-line helpers) and
`StudioProtocolTestHelpers.h` (`roundTrip`, `roundTripTree`).

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
