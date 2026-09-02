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
replies with its task id and its body runs later as a Server Task. A sync op
the client sent after a task waits until that task has run; task requests,
`CancelTask` and Remote Browse do not wait.

Server Tasks (`server/ServerTaskRunner`) run on the render loop, one per
iteration, to completion; frames pause while one runs (the client keeps its
last frame). `TaskProgress` is indeterminate with phase text; `CancelTask`
removes a task still queued (`TaskFailed{"cancelled"}`) and is refused for
the running one ("task already running"). Queued tasks die with the session
they were sent on. `OpenProject` goes through `stageProjectOpen` then
`ProjectContext::openStagedProject`, both on the loop thread, so a later
worker-thread staging phase is a mechanical move. Task ids increase for the
life of the server; `TaskCompleted::message` of an import carries the new
dataset's id.

Not there yet (milestones 6-7): playback and time, picking, viewport passes
and `RenderShot`. The server answers those messages with `Error{"... not
implemented in this server"}`.

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
- **Not in the v1 message set, hence read-only in the client:** light node
  rename (node and object names are not parameters), camera rig clone, and
  camera rig keyframe editing (Set View, Capture, Update, Delete); an
  `UpdateCameraRig{CameraRig}` analogous to `UpdateShot` would cover the
  last. `UpdateShot` covers every Shot field except `playing`, which stays
  with playback (`SetPlaying`, milestone 6).
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

## Tests

`vsrTests "[StudioProtocol]"` (codecs), `"[StudioClient]"` (client core
against a fake server), `"[StudioServer]"` (server against a raw
`NetworkClient`: session, scene edits, and in
`test_StudioServerProjectOps.cpp` the project ops, Remote Browse, Server
Tasks and Data Roots), `"[StudioRemote]"` (server and client core in one
process) and `"[StudioTestClient]"` (the test client's script runner against
an in-process server). Those that render use `helide` and skip when it
cannot be loaded. The new `ProjectContext` operations are covered by
`"[SciVisStudio]"`.

End to end, `ctest -R StudioScenario` runs each scenario script under
[`test_client/scenarios/`](test_client/scenarios) against a freshly launched
`scivisStudioServer`; see [`test_client/README.md`](test_client/README.md)
for the headless test client, its command vocabulary and how to run a
scenario by hand.
