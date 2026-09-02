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
state, Project Snapshot), raw and turbojpeg frames with header and encoding
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
after a task waits until that task has run; task requests do not wait, and
`CancelTask` and Remote Browse are served even from behind a waiting sync op
(they touch neither Project nor Scene).

Server Tasks (`server/ServerTaskRunner`) run on the render loop, one per
iteration, to completion; frames pause while one runs (the client keeps its
last frame). `TaskProgress` is indeterminate with phase text; `CancelTask`
removes a task still queued (`TaskFailed{"cancelled"}`) and is refused for
the running one ("task already running"). A body that throws (a filesystem
error on a path the roots admitted) fails its task rather than the server.
Queued tasks die with the session they were sent on, and the client drops its
task records at every `BootstrapBegin` for the same reason (milestone 7's
task-status replay refills them). `OpenProject` goes through `stageProjectOpen` then
`ProjectContext::openStagedProject`, both on the loop thread, so a later
worker-thread staging phase is a mechanical move. Task ids increase for the
life of the server; `TaskCompleted::message` of an import carries the new
dataset's id.

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

Milestone 6, playback (item 6): `SetPlaying` is a Project Op and `SetTime` a
Control-State Latch slot; the server free-runs the AnimationManager on its
loop thread, commits Time at Rest through snapshots (auto-stop, debounced
scrub) and reports frames a file binding cannot load as
`TimeAdvanceWarning`. Picking and the viewport passes (item 6): `Pick`,
`SetOutline` and `ViewportSettings` are latch slots (latest-wins);
`RequestArrayHistogram` is a sync Project Op. See "Viewport" below.

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
  count): `binCount` is clamped to `[1, 4096]`, the last bin is closed, and
  equal min and max put everything in bin 0. Fixed-point element types count
  in ANARI's normalized range. Refused with an error: references that are not
  arrays, proxy arrays (the mirror's descriptors), CUDA arrays and non-scalar
  element types (vectors, matrices, object handles). No snapshot follows.

Not there yet (milestone 7): `RenderShot` (60). The server refuses it, and
any request whose payload it cannot decode, loudly rather than dropping
them: with a `ProjectOpReply{ok=false}` carrying the request's id when the
payload has a readable non-zero `requestId` (so a client's pending request
retires), and with a bare `Error{"... not implemented in this server"}` /
`Error{"malformed ..."}` otherwise. The client core covers the second case too: a bare `Error` that
names the type of a pending request fails the oldest pending request of that
type, so no control stays greyed until the connection is lost.

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
UI-state tree in the monolith's shape; restoring it on open is a later
milestone.

## Open design questions

- **Color map objects.** No scene object type today carries a color map as
  *parameters*: vsr_ui_imgui's TransferFunctionEditor edits a
  `vsr::scene::Array` of RGBA samples (`colormap_<name>`). The server
  therefore pairs each `ColorMapRecord` with a `vsr::scene::Array` of
  `ANARI_FLOAT32_VEC4` (256 samples, default color map) named
  `<colorMapId>_colormap`; the name is the only link, as with
  `<shotId>_camera`. Consequences the transfer-function work must resolve:
  the samples are array data, not parameters, so `SetObjectParameter` cannot
  edit them and the bootstrap's descriptor-only `TransferScene` does not
  carry them; project saves persist the record but not the Array, so an
  opened project recreates each record's Array with default samples. Rename
  touches the record only.
- **Renderer library switches.** `UpdateShot` accepts a
  `renderSettings.rendererObjectIndex` only when it names a Renderer of
  `renderSettings.rendererLibrary` (or is unset). The server renders with
  its own library regardless and rewrites the active shot's renderer
  settings to match when they disagree, as `setupRendering` always did, so
  a client choosing another library sees its choice overridden in the next
  snapshot rather than a device switch.
- **Task threading.** Moving the disk phases of tasks to a worker thread
  (the split `stageProjectOpen`/`openStagedProject` prepares) is deferred;
  see the notes on which `ProjectContext` calls are staging-safe.

- **Light rename, camera-rig clone, camera-rig keyframe editing.** The v1
  message set has no op for renaming a light node (node and object names are
  not parameters), cloning a camera rig, or editing a camera rig's keyframes
  and current pose (the monolith's Set View, Capture, Update, Delete, pose
  editor and inline frame/name/interpolation edits). The client shows these
  read-only with a tooltip saying so. Candidates: `RenameLightNode{lightRigId,
  lightNode, name}`, `CloneCameraRig{cameraRigId}`, and an
  `UpdateCameraRig{rig}` whole-value replace mirroring `UpdateShot`; the
  viewport's manipulator pose is what Set View/Capture would send.
  `UpdateShot` covers every Shot field except `playing`, which stays with
  playback (`SetPlaying`, milestone 6).
- **Naming a loaded Dataset Archive.** `LoadDatasetArchive` carries no name,
  so the Add Static Dataset dialog applies a typed name with a follow-up
  `RenameDataset` once the snapshot after the load shows exactly one new
  dataset id. A `name` field on the request would remove the heuristic.
- **Renderer libraries in the Shot Editor.** The client offers only the
  device names of the Renderer objects in the Structural Mirror (plus the
  shot's current value); the server's loadable-library list is not in the
  protocol.

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
