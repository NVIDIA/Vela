# scivisStudioTestClient

A second, complete Studio client with no UI: a command-line program that
speaks the Studio Message Set from a script so `scivisStudioServer` can be
exercised end to end on a machine with no display, in CI, or by an agent
working unattended. It exists so every server milestone can be verified
without launching the interactive client, and so the protocol has two
independent client implementations: `test_client/` shares only
`vsr_scivis_studio_protocol` (and through it the transport and model types)
with `client/`, and implements its own connection, handshake, Bootstrap,
liveness and loss logic (`TestSession`). It keeps the same client-held state
as the GUI client, a Structural Mirror and a Project Replica, so scripts can
assert on what the server pushed, not just on which message types arrived.

Design: [`docs/scivis-studio-client-server.md`](../../../../../docs/scivis-studio-client-server.md),
section "Headless test client"; vocabulary: [`CONTEXT.md`](../CONTEXT.md).

Not a load or fuzz tool: one connection, sequential commands, no timing
measurement beyond deadlines.

## Command line

```
scivisStudioTestClient [--host H] [--port N] [--timeout MS] [--keep-going]
                       [--quiet-events] [--script FILE | -e "cmd; cmd" ...]
```

| flag | meaning |
|------|---------|
| `--host H`, `--port N` | endpoint `connect` uses when the command names none (127.0.0.1:12345) |
| `--script FILE` | read commands from FILE, one per line |
| `-e "cmd; cmd"` | run these commands; repeatable, in order (cannot be combined with `--script`) |
| `--timeout MS` | deadline of every waiting command unless it carries `timeout=MS` (5000) |
| `--keep-going` | continue after a FAIL; the exit status is still non-zero |
| `--quiet-events` | print no `EVT` lines except those of the `dump-*` commands |

Without `--script` or `-e` the script is read from stdin. Exit status: 0 iff
every command printed `OK`; 1 when any FAILed; 2 for a bad command line or a
script that does not parse. The record stream is stdout alone; transport and
session log lines go to stderr.

### Script syntax

One command per line; `#` starts a comment; blank lines are ignored; `;`
separates several commands on one line. Tokens split on whitespace; double
quotes group a token containing spaces and are dropped (`""` is the empty
string; `name="Two words"` is one token). Waiting commands take a trailing
`timeout=MS` that overrides `--timeout` for that command only.

`$name` anywhere in an argument expands to a variable (letters, digits and
underscores end the name, so `$dataRoot/project` works); an unknown
variable FAILs the command. The variables are the ids replies mint, listed
under [Variables](#variables). In the value position of `assert`, write
`var.<name>` instead: `assert var.lastTaskId == 1`.

## Commands

The vocabulary is the server surface through milestone 7: the session,
rendering and scene-edit commands below, one request command per Project
Op, Remote Browse and task message, the playback, picking and viewport
commands, the offline render and the UI state round trip, and the waits
that go with them. An
`Error` the server sends while any command but `expect-error` runs FAILs that
command with the Error's text (only Project Ops carry request ids, so for
everything else the command in flight is the attribution); a waiting command
also FAILs as soon as the connection is Lost, naming the loss, rather than
at its deadline. Write the Errors a script provokes as expectations
(`send-raw 60` then `expect-error`), and let the reply of a fire-and-forget
command settle (`sleep`) before the next one if its Error is to land on it.

### Session, rendering and scene edits

| command | does |
|---------|------|
| `connect [HOST] [PORT]` | TCP connect, exchange Hellos (exact `PROTOCOL_VERSION` match), await the complete Bootstrap |
| `disconnect` | send Disconnect and close; mirror and replica are cleared -> Disconnected |
| `shutdown` | send Shutdown and await the server closing the socket -> Disconnected |
| `ping` | send Ping; the Pong is for `expect-pong` |
| `expect-pong` | the next non-Frame server message must be a Pong |
| `await-lost` | wait until the connection is Lost (mirror and replica stay as a frozen view) |
| `reconnect` | connect again to the last host and port, retrying refused attempts until its deadline (a server being restarted refuses at once); a fresh handshake and Bootstrap |
| `sleep MS` | keep polling (events still print) for MS milliseconds; a loss meanwhile is the next command's to notice. MS, like every `timeout=MS`, must fit a deadline |
| `expect-error [SUBSTRING]` | the next server message other than a Frame or a liveness Pong must be an Error, containing SUBSTRING if given |
| `send-raw TYPE [HEX ...]` | send a message of type byte TYPE (0..255) with the given payload bytes, verbatim |
| `set-frame-config W H` | request a frame size, await the FrameConfig ack |
| `set-encodings NAME[,NAME]` | offer frame encodings, most preferred first (`raw`, `turbojpeg`; case-insensitive) |
| `start-rendering` | ask the server to stream frames |
| `stop-rendering` | pause the stream |
| `await-frame [COUNT]` | wait for COUNT (default 1) further frames |
| `await-frame-at FRAME` | wait for a Frame whose header says `frame == FRAME`; the headers meanwhile print as they come |
| `await-frame-advance [COUNT]` | wait for COUNT (default 1) further frames whose header `frame` differs from the previous frame's |
| `save-frame PATH.ppm` | decode the newest frame into a binary P6 PPM (relative to the working directory) |
| `set-param TYPE INDEX NAME ANARITYPE VALUE...` | optimistic edit: set a parameter on the mirror and on the wire (`camera 1 fovy float32 0.9`, `camera 1 position float32_vec3 1 2 3`, `... note string "two words"`, `... flag bool true`) |
| `remove-param TYPE INDEX NAME` | remove a parameter, mirror and wire |
| `set-node-transform LAYER NODE M0..M15` | set a transform node's matrix, column-major; NODE is the server's node index |
| `dump-scene` | one `EVT Object` line per mirror object: type, index, subtype, name, params count |
| `dump-layers` | one `EVT Layer` line per mirror layer: index, name, nodes, active |
| `dump-project` | one `EVT Project` line from the replica (name, activeShot, counts, dirty, directory), then one `EVT Shot`, `EVT Dataset`, `EVT LightRig`, `EVT CameraRig` and `EVT ColorMap` line per entity |
| `dump-frame` | one `EVT Frame` line for the newest frame header |
| `set-ui-state KEY=VALUE... \| none` | build the UI state tree the next `save-project`s send: one string leaf `windows/<key>` per edit, repeated commands composing (a key set again is overwritten); `none` drops the tree, so `save-project` sends none again and the server keeps the tree the project opened with |
| `dump-ui-state` | one `EVT UIState present= children=` line for the newest tree the server sent, then one `EVT UIStateEntry path= value=` per leaf (`windows/layout`) |
| `find-object TYPE [first\|name=NAME]` | the first mirror object of that type, or the first one so named: one `EVT Object` line, and `$lastObjectRef` (`type:index`), `$lastObjectType`, `$lastObjectIndex`; every array kind is looked up in the one `array` pool |
| `assert VALUE OP RHS` | compare a named value; OP in `== != < <= > >= contains`; RHS is a literal, or `@NAME` for another named value (`assert shot.active.currentFrame == @frame.frame`) |

Object TYPE and ANARITYPE are the ANARI names without the `ANARI_` prefix,
case-insensitive (`camera`, `light`, `float32`, `float32_vec3`, `int32`,
`bool`, `string`). Vector, matrix and box values take one token per
component. Where a command takes an object as `TYPE INDEX`, the one-token
`TYPE:INDEX` spelling `$lastObjectRef` expands to is accepted as well
(`set-outline`, `request-array-histogram`).

### Playback, picking and the viewport

Time in motion arrives in the Frame headers; Time at Rest in the Project
Snapshot's active shot (`await-snapshot` prints its `playing=` and
`currentFrame=`). `SHOT` below is a shot id or `active`, the replica's
active shot.

| command | does |
|---------|------|
| `set-playing SHOT on\|off` | Project Op: start or stop playback of the active shot (another id is refused); the reply and a snapshot follow |
| `set-time SHOT FRAME` | one-way scrub (latest-wins); while paused, the server commits Time at Rest with one debounced snapshot once no `SetTime` has arrived for 250 ms and the frame changed. A `SHOT` that is not active is ignored by the server, silently |
| `await-warning` | wait for the next `TimeAdvanceWarning` (a frame that failed to load while playing); prints `EVT TimeAdvanceWarning shotId= frame= message=` |
| `pick X Y` | send a `Pick` at that pixel (x right, y down from the top-left, in frame-header pixels; the server clamps what lies outside the frame) and await its `PickReply`, printed as `EVT PickReply requestId= hit= worldPosition="x y z" objectType= objectIndex=` (`none` when nothing was hit); sets `$lastPickType`, `$lastPickIndex` on a hit and unsets them on a miss |
| `set-outline [TYPE INDEX\|TYPE:INDEX\|none]` | outline that object, or clear the outline (`none` or no argument) |
| `viewport-settings KEY=VALUE...` | edit the remembered `ViewportSettings` and send the whole struct; keys `highlightSelection`, `outlinePrimitives`, `showWorldBounds`, `edgeInvert` (bools), `worldBoundsColor=r,g,b,a`, `worldBoundsWidth`, `visualizeAOV` (`NONE`, `DEPTH`, `ALBEDO`, `NORMAL`, `EDGES`, `OBJECT_ID`, `PRIMITIVE_ID`, `INSTANCE_ID`), `depthVisualMinimum`, `depthVisualMaximum`; an unknown key is a usage error. Unset keys keep the last value sent (the struct's defaults at first), so repeated commands compose; with no edits the current struct is sent again |
| `request-array-histogram TYPE INDEX BINS` | Project Op: bin a scalar array on the server; the reply prints `bins=<count> min= max= nonFinite=` and fills the `histogram.*` values (cleared when the request goes out, so a refused one leaves none) |

`pick` is request/reply like a Project Op but its reply is a plain
`PickReply`, not a `ProjectOpReply`: `expect-fail` and `no-wait` do not apply
to it. `set-time`, `set-outline` and `viewport-settings` are optimistic: they
send and print whatever events were queued.

### Project Ops, Remote Browse and tasks

Every request command mints a request id, sends the request, waits for the
`ProjectOpReply` with that id (deadline) and prints it as `EVT ProjectOpReply
requestId= ok= error= <result fields>`, the result fields decoded from the
reply (`shotId=`, `taskId=`, `lightNode=studio:8`, `object=array1d:3`,
`roots=`, `entries=`, `candidates=`). `ok=false` FAILs the command with the
server's error unless the command is prefixed with `expect-fail`, when
`ok=true` FAILs instead (`expect-fail remove-shot shot_9999`). Scene pushes
caused by the op (`ObjectAdded`, `TransferLayer`) arrive before the reply;
the `ProjectSnapshot` that confirms a mutation arrives after it, so a script
runs `await-snapshot` before asserting on the replica. A failed op sends no
snapshot; some "failed" ops still mutate (an import that leaves an
`ImportFailed` record) and do.

Task-launching ops answer at once with `taskId=` and run on the server's loop
one at a time; `await-task` waits for the `TaskCompleted`/`TaskFailed`, the
`TaskProgress` lines printing as they come (`current= total=`, determinate
for a render, `total=0` otherwise), and the end line carrying
`framesCompleted=` when the task wrote frames. The completion message lands
in `$lastTaskMessage`: an import's is the new dataset's id (then also
`$lastDatasetId`), a render's the output directory.

A refused request FAILs the command with the server's reason; written as an
expectation, the reason is still there to check:

```
expect-fail render-shot active
assert lastReplyError contains saved
```

The `no-wait` prefix sends without awaiting the reply, so several requests
can be in flight (two imports and the cancel of the second before it runs);
`await-reply` collects them, oldest first. A no-wait reply that an earlier
command already drained is decoded when collected but its record is not
printed again.

| command | does |
|---------|------|
| `new-project` | sync: an unsaved empty project replaces the current one |
| `open-project DIR` | task: open the project stored in DIR |
| `save-project [DIR]` | task: save to DIR, or to the project's own directory |
| `import-static-dataset PATH [NAME] [IMPORTER]` | task; IMPORTER is a `vsr::io::ImporterType` name (`OBJ`, `PLY`, ...) or `VSR_SUBTREE` for a subtree archive |
| `import-file-animation-dataset NAME IMPORTER PATH... [set-frame-count=BOOL]` | task |
| `declare-file-animation-dataset NAME IMPORTER SOURCE... [set-frame-count=BOOL]` | sync; the reply carries `datasetId=` |
| `reimport-dataset ID`, `load-dataset ID` | tasks |
| `rename-dataset ID NAME`, `unload-dataset ID`, `refresh-dataset-availability ID` | sync |
| `remove-dataset ID [keep-asset-file]` | sync; removes the saved asset file too unless told to keep it |
| `save-dataset-archive ID PATH`, `load-dataset-archive PATH` | tasks |
| `discover-dataset-candidates` | sync; one `EVT DatasetCandidate file= proposedName=` per candidate |
| `incorporate-dataset-candidate FILE [PROPOSED] [NAME]` | task |
| `create-shot [NAME]` | sync; `shotId=`; the new shot becomes active |
| `remove-shot ID`, `set-active-shot ID` | sync |
| `set-playing SHOT on\|off`, `request-array-histogram TYPE INDEX BINS` | sync; see [Playback, picking and the viewport](#playback-picking-and-the-viewport) |
| `update-shot ID FIELD=VALUE...` | sync: the replica's Shot with the edits applied is sent whole; fields `name`, `frameCount`, `fps`, `loop`, `currentFrame`, `lightRigId`, `cameraRigId`, `renderSettings.{width,height,samples,rendererLibrary,rendererSubtype,rendererObjectIndex,outputFilePrefix}`, `binding.<datasetId>=on\|off` (`playing` is refused: it belongs to playback) |
| `create-light-rig [NAME]`, `clone-light-rig ID`, `load-light-rig-archive PATH` | sync; `lightRigId=` |
| `remove-light-rig ID`, `rename-light-rig ID NAME`, `save-light-rig-archive ID PATH` | sync |
| `add-light RIG [SUBTYPE]` | sync; `lightNode=LAYER:NODE`; SUBTYPE defaults to `directional` |
| `remove-light RIG LAYER NODE` | sync; the node reference `add-light` returned |
| `create-camera-rig [NAME]`, `load-camera-rig-archive PATH` | sync; `cameraRigId=` |
| `remove-camera-rig ID`, `rename-camera-rig ID NAME`, `save-camera-rig-archive ID PATH` | sync |
| `create-color-map [NAME]` | sync; `colorMapId=` and `object=TYPE:INDEX`, the record and its scene-side Array |
| `rename-color-map ID NAME`, `remove-color-map ID` | sync |
| `list-roots` | one `EVT DataRoot path=` per Data Root; the first is `$dataRoot` |
| `list-directory PATH` | one `EVT DirectoryEntry name= kind= size= mtime=` per entry (`File`, `Directory`, `ProjectDirectory`); FAIL (or `expect-fail`) outside every root |
| `render-shot SHOT` | task: render the shot's frames offline (`SHOT` an id or `active`); needs a saved project, refused while another render is queued or running. The shot becomes active first (a snapshot precedes the progress), the progress is determinate (`current=<frame> total=<frames>`), the end carries `framesCompleted=` and, when completed, the output directory as its message. While it is queued or running, mutating requests that reach dispatch are refused with "render in progress" and interactive frames pause; a request that arrives while the render body holds the loop is latched and dispatched once it returns, so it is refused only when another render is queued by then |
| `cancel-task TASKID` | sync; removes a queued task (it then ends as `TaskFailed "cancelled"`), or asks a running render to stop at its next frame (the reply is ok once it has; the task ends as `TaskFailed "cancelled"` with `framesCompleted=` the frames left on disk); a finished task is an error reply |
| `await-task [TASKID] [expect-fail]` | wait for the task (default `$lastTaskId`) to end; FAIL on `TaskFailed` unless `expect-fail` follows, then FAIL on `TaskCompleted` |
| `await-task-progress [TASKID]` | wait until the task (default `$lastTaskId`) has reported progress at least once (a report an earlier command already printed counts); FAIL at once when it ends without any |
| `await-snapshot` | wait for a `ProjectSnapshot` newer than the last thing awaited: the reply to the last request command, the end of the last `await-task`, or the previous `await-snapshot`. Each `await-snapshot` consumes one snapshot, so two that land together (a `set-playing`'s and the auto-stop's) are awaited one at a time. A snapshot that arrived before the mark belongs to something earlier, so await a task that sends none (a cancelled one) before the one whose snapshot is wanted |
| `await-reply [REQUESTID]` | collect the reply of a `no-wait` request, the oldest pending by default |

### Variables

| variable | set by |
|----------|--------|
| `$lastShotId`, `$lastLightRigId`, `$lastCameraRigId`, `$lastColorMapId` | the `*CreatedResult` of the create, clone and archive-load replies |
| `$lastObjectRef` (`type:index`), `$lastObjectType`, `$lastObjectIndex` | `create-color-map`'s scene-side object, and `find-object` |
| `$lastPickType`, `$lastPickIndex` | the identity a `pick` hit (unset again by a miss) |
| `$lastLightLayer`, `$lastLightNode` | `add-light` |
| `$lastDatasetId` | `declare-file-animation-dataset`'s reply, and the completion message of an import, archive-load or incorporate task after `await-task` (a message that names a dataset id, `dataset_...`) |
| `$lastTaskId` | every task-launching reply |
| `$lastTaskMessage` | the completion message of the task `await-task` last saw complete (a render's output directory, an import's dataset id) |
| `$lastRequestId` | every request sent |
| `$dataRoot` | `list-roots`: the first Data Root |

### Assert values

Numbers compare numerically (at float32 precision for equality, so `== 0.9`
holds for a parameter set from `0.9`); everything else compares as text.
Numeric parameter values print as the shortest text that reads back as the
same number (`0.9`, `0.12345679` for a float32 set from `0.123456789`), so
`==` holds for whatever literal a parameter was set from. Integer components
must fit their type (`uint8 300` is a FAIL, not 44) and `send-raw` payload
bytes are exactly two hex digits each.

| value | is |
|-------|----|
| `state` | `NeverConnected`, `Connected`, `Lost` or `Disconnected` |
| `scene.objects`, `scene.layers`, `scene.cameras`, `scene.renderers` | counts in the Structural Mirror |
| `project.name`, `project.directory`, `project.activeShot`, `project.dirty` | Project Replica fields (FAIL before the first snapshot) |
| `project.shots`, `project.datasets`, `project.lightRigs`, `project.cameraRigs`, `project.colorMaps` | collection sizes in the replica |
| `shot.<id>.<field>` | `name`, `frameCount`, `fps`, `currentFrame`, `loop`, `playing`, `lightRigId`, `cameraRigId`, `camera` (`type:index`), `bindings` (count), `binding.<datasetId>` (`true`/`false`; FAIL when unbound), `renderSettings.{width,height,samples,rendererLibrary,rendererSubtype,outputFilePrefix}`; an unknown id is a FAIL; `shot.active.<field>` names the active shot |
| `dataset.<id>.<field>` | `name`, `status` (`Available`, `Unavailable`, `Importing`, `ImportFailed`), `residency` (`Loaded`, `Unloaded`), `sourceKind`, `importerType`, `sourcePath`, `dirty`, `declared`, `rootNode` |
| `lightRig.<id>.name`, `cameraRig.<id>.name`, `colorMap.<id>.name` | names in the replica |
| `tasks.completed`, `tasks.failed` | `TaskCompleted` / `TaskFailed` messages received since the session object was made, across reconnects, the ones a Bootstrap replays included (so an end heard live and then replayed counts twice). The "connection lost" failures a `BootstrapBegin` declares (below) are not messages and do not count |
| `tasks.replayed` | task messages (progress or end) the newest Bootstrap carried between its Begin and End: the server's task-status replay of what ended since the previous Bootstrap, and the running task's status. 0 again at every `BootstrapBegin` |
| `task.<id>.<field>`, `task.last.<field>` | a task record (`last` is `$lastTaskId`; FAIL when nothing has been heard of the id): `state` (`Queued` from the launching reply, `Running` from the first `TaskProgress`, `Completed`, `Failed`), `message` (the completion message or the failure's error), `framesCompleted`, `current`, `total` (the newest progress; 0 when indeterminate) |
| `uiState.present`, `uiState.<key>` | the newest `UIState` tree the server sent (a Bootstrap's, or the one that follows an `open-project`): whether there is one, and the string leaf `windows/<key>` in it, as `set-ui-state` writes it (FAIL when there is no tree or no such leaf). `disconnect` forgets the tree |
| `replies.failed`, `replies.pending`, `lastReplyError` | replies with `ok=false`, `no-wait` requests awaiting collection, and the error text of the newest failed reply |
| `snapshots.received` | Project Snapshots applied, the Bootstrap's included |
| `browse.entries` | entries of the last `list-directory` (0 after a refused one) |
| `var.<name>` | a variable's value (see [Variables](#variables)) |
| `frame.width`, `frame.height`, `frame.encoding`, `frame.shotId`, `frame.frame` | header of the newest frame (FAIL before the first frame); encodings read `Raw`, `TurboJpeg` |
| `frames.received` | frames consumed so far (frames superseded before they were read are not counted) |
| `frames.advanced`, `frames.maxStep` | consumed frames whose header `frame` differed from the previous one's, and the largest forward step between two consecutive headers. A step backwards (a loop wrap to 0, a scrub back) is time moving on purpose, not a skip, and does not count; a forward scrub does. Frames are latest-wins, so a client slower than the stream can see steps the server never took |
| `warnings.received`, `lastWarning` | `TimeAdvanceWarning`s received and the message of the newest one |
| `pick.hit`, `pick.worldPosition`, `pick.objectType`, `pick.objectIndex` | the last `PickReply` (FAIL before one): `true`/`false`, `"x y z"`, and the identity (`surface`/`volume` and the pool index, or `none`) |
| `histogram.bins`, `histogram.min`, `histogram.max`, `histogram.total`, `histogram.nonFinite` | the last ok `request-array-histogram`: the bin count, value range, sum of all bins and the NaN/inf elements left out of them (FAIL when the last request was refused or none was made) |
| `frameConfig.width`, `frameConfig.height` | the last FrameConfig the server acknowledged |
| `param.<type>.<index>.<name>` | a mirror parameter's value: strings verbatim, bools `true`/`false`, numbers space-separated per component (`"1 2 3"`), object references `type:index`; a missing parameter is a FAIL |
| `errors.received`, `lastError` | Error messages received and the text of the newest one |

### Task records across a session

A task is known from the reply that launched it (`Queued`), moves to
`Running` with its first `TaskProgress`, and ends with the one
`TaskCompleted`/`TaskFailed`. A `BootstrapBegin` (a `reconnect` after a
loss) fails every record still open with the message `connection lost`: its
end, if any, went to a closed socket. The server's task-status replay inside
that Bootstrap then rebuilds what it still knows -- the end of every task
that finished since the previous Bootstrap, and one `TaskProgress` for a
running one -- so `task.<id>.state` after a `reconnect` is the truth as the
server has it, and a task the restarted server never heard of stays
`Failed "connection lost"`. `disconnect` clears every record, so a task
launched before it is known again only through the replay. Task ids count
from 1 for the life of a server process: after a kill and restart, ids repeat,
and a record of the old process with the same id is overwritten by the new
task's messages.

## The record stream

Every line is one record: exactly one `OK <command>` or `FAIL <command>:
<reason>` per command, and one `EVT <Name> key=value ...` per server message
as the session consumes it (Frames are latest-wins, so a fast stream shows
fewer `EVT Frame` lines than the server sent). `<command>` is the command as
written, so the output can be grepped for the script's own lines.

```
EVT Hello version=1 buildInfo="scivisStudioServer/helide"
EVT BootstrapBegin
EVT TransferScene objects=5 layers=1
EVT TransferLayer objects=5 layers=1
EVT FrameConfig width=1024 height=768
EVT ProjectSnapshot activeShot=shot_0001 shots=1 datasets=0
EVT BootstrapEnd
OK connect
OK set-encodings raw
EVT FrameConfig width=64 height=48
OK set-frame-config 64 48
OK start-rendering
EVT Frame width=64 height=48 encoding=Raw pixelFormat=RGBA8_sRGB shotId=shot_0001 frame=0 bytes=12288
EVT Frame width=64 height=48 encoding=Raw pixelFormat=RGBA8_sRGB shotId=shot_0001 frame=0 bytes=12288
OK await-frame 2
FAIL assert frame.width == 32: frame.width is "64", not == "32"
```

## Scenarios

`scenarios/*.studio` are self-contained scripts: each starts with `connect`
and ends with `disconnect` or `shutdown`, and passes against
`scivisStudioServer --library helide --data-root <tmp>` on a fresh unsaved
project. The milestone-5 scripts take the data root from `list-roots`
(`$dataRoot`) and every id from a reply, so they name no path or id of their
own except those a fresh server mints deterministically (`shot_0001`,
`dataset_0002` for an import whose task failed, task ids from 1).

| scenario | covers |
|----------|--------|
| `session.studio` | Bootstrap asserts, ping, disconnect, reconnect, shutdown |
| `frames_raw.studio` | Raw encoding, frame config ack, streaming, header asserts, `save-frame` |
| `frames_turbojpeg.studio` | the same with TurboJpeg; needs `VSR_USE_TURBOJPEG` at both ends |
| `scene_edits.studio` | `set-param`/`remove-param` on the active shot's camera, mirror asserts, frames still arrive |
| `errors.studio` | `send-raw` of unknown, unassigned and not-yet-implemented types, each answered with an Error |
| `loss.studio` | the server killed externally: `await-lost`, frozen mirror, `reconnect` to a restarted server |
| `project_lifecycle.studio` | `new-project`, shots created, switched, edited (`update-shot`, clamping, a refused rig id) and removed (the last one refused), `save-project` as a task, `open-project` of the result |
| `rigs.studio` | light rigs: create, `add-light`/`remove-light`, rename (collisions refused), clone, archive save and load, bind to the active shot, remove; camera rigs likewise |
| `color_maps.studio` | create (record plus scene-side Array), rename, remove, refusals on a removed id |
| `datasets.studio` | import of the fixture OBJ, a failed import's `ImportFailed` record, rename, shot bindings, a declared file animation, Dataset Archives, unload/refresh/load on a saved project, candidates discovered and incorporated |
| `tasks.studio` | a task's progress and completion, two imports queued with the second cancelled before it runs (`no-wait`, `await-reply`), cancel errors |
| `browse.studio` | `list-roots`, listings inside the root (a saved project marked `ProjectDirectory`), refusals outside the roots, on a file and on a missing directory |
| `errors_project.studio` | unknown ids of every collection, paths outside the Data Roots, an unsaved project's `save-project`, an `open-project` task that fails; nothing changes |
| `all_m5.studio` | the milestone-5 surface in one session: a project with a dataset, rigs, a color map and a bound second shot is saved, replaced by `new-project`, reopened and checked collection by collection; a sync op queued behind an import waits for it (the new shot binds the imported dataset); a cancelled task ahead of a sync op does not hold it up; frames follow the active shot throughout |
| `playback.studio` | a 12-frame looping shot at an fps ceiling of 100 (well below the loop rate, so a dropped in-flight frame cannot look like a skipped tick): `set-playing on`, five frame advances with no forward step above 1, `set-playing off` and its rest snapshot agreeing with the next headers; playing an unknown shot refused |
| `autostop.studio` | a 6-frame non-looping shot plays to its end: the auto-stop's snapshot (`playing=false`, `currentFrame=5`) arrives unrequested, and frames stay on the last frame |
| `scrub.studio` | paused scrubs: the later of two `set-time`s wins, its frame shows in the header and one debounced rest snapshot follows; a `set-time` for another shot moves nothing; scrubbing to the committed frame commits nothing |
| `pick.studio` | the fixture triangle under an aimed camera: a centre pick hits a surface whose identity feeds `set-outline`, a corner pick misses, out-of-frame coordinates are clamped, the outline is cleared |
| `viewport.studio` | `viewport-settings` composed step by step (depth view, world bounds, edges, primitive outlines, back to plain) with frames streaming through each |
| `histogram.studio` | the fixture's vector array, a missing array and a camera each refused by `request-array-histogram`; the binning itself waits for a scalar fixture |
| `render_shot.studio` | a three-frame render of the fixture at 32x24: determinate progress, `framesCompleted == 3`, the output directory as the message (listed: three files), then the refusals on an unsaved project and an unknown shot |
| `render_cancel.studio` | a 400-frame render cancelled after its first progress: a second `render-shot` and a `create-shot` sent meanwhile are latched behind the body and dispatched together after the cancel, so the second queues and the `create-shot` is refused with "render in progress"; the cancel's reply is ok, the task ends `Failed "cancelled"` with the frames written so far, the second render is cancelled too, and edits go through again afterwards |
| `task_replay.studio` | a 60-frame render left running by a `disconnect`: the `reconnect` is bootstrapped once the render is done and its Bootstrap replays the `TaskCompleted` (`tasks.replayed >= 1`, `task.last.state == Completed`); the next Bootstrap replays nothing |
| `ui_state.studio` | `set-ui-state` leaves saved with the project, absent on a fresh project, back after `open-project` (a `UIState` before the task's end) and in every later Bootstrap, and kept by a save that sends no tree |
| `loss_during_task.studio` | the server killed while a 400-frame render of this client runs (kill-restart mode): Lost keeps the record `Running`, the reconnect's Bootstrap fails it with `connection lost`, the restarted server replays nothing |

### By hand

Start a server, then point the client at its port:

```bash
export LD_LIBRARY_PATH=<anari prefix>/lib:<libjpeg-turbo prefix>/lib
scivisStudioServer --library helide --data-root /tmp/studio-data --port 12345 &
scivisStudioTestClient --port 12345 --script test_client/scenarios/session.studio
scivisStudioTestClient --port 12345 -e "connect; start-rendering; await-frame 3; dump-frame; shutdown"
```

Or let `scenarios/run_scenario.sh` do it: it picks a free port (picking again
when another process grabs it before the server binds), starts the server
with `--library helide --data-root <mktemp -d>` in a temporary directory
(where `save-frame` files land), waits for its `Listening on port` line, runs
the client with `--script`, propagates the client's exit code, and always
stops the server. On success the temporary directory is removed; on failure
both logs are printed and the directory is kept, its path in the output, for
a post-mortem. Extra arguments go to the server after `--library helide
--data-root <tmp>` (a `--project`, say); do not pass `--port`, the runner owns
it.

```bash
test_client/scenarios/run_scenario.sh build/scivisStudioServer \
  build/scivisStudioTestClient test_client/scenarios/frames_raw.studio
```

`datasets.studio`, `tasks.studio`, `pick.studio`, `histogram.studio` and the
render scenarios carry the hint `# runner: fixture fixtures/triangle.obj` in
their opening comment block: the runner copies the file (relative to the
scenario) into the data root before the server starts, so the script imports
`$dataRoot/triangle.obj` without writing anything itself. The hint may
repeat, and combine with the one below.

`loss.studio` carries the hint `# runner: kill-restart-after 3` in its
opening comment block: once the client has printed three `OK` records the
runner kills the server (SIGKILL) and starts a new one on the same port,
which is what the script's `await-lost` and `reconnect` need. The runner
gives the restarted server 30 s to reach `Listening on port` and the
script's `reconnect timeout=45000` outlasts that, retrying while the port is
refused, so no fixed sleep is involved. Run without the runner, `await-lost`
FAILs after its deadline because nothing kills the server.
`loss_during_task.studio` uses the same mode with a count that lands right
after its render has reported progress: the count is the number of `OK`
records the script prints up to and including `await-task-progress`, so a
command added before that point moves it.

### Under ctest

`test_client/CMakeLists.txt` registers one `vsr::StudioScenario.<name>` test
per scenario when `BUILD_TESTING` is on (`frames_turbojpeg` only with
`VSR_USE_TURBOJPEG`), each running `run_scenario.sh` with a 120 s timeout.
The runner exits 77 when the server can load no ANARI device, which ctest
reports as a skip (`SKIP_RETURN_CODE 77`) rather than a failure.

```bash
LD_LIBRARY_PATH=<anari prefix>/lib ctest --test-dir build -R StudioScenario -j 8
```

## Adding commands and scenarios

Each later milestone lands with the test client commands and scenarios that
exercise its server surface, so the scenarios stay the acceptance test of the
whole protocol:

1. Add the session-level operation to `TestSession` (send, plus any inbound
   handling and its `Event`), then the command to `CommandRunner`: a
   `Command`-taking member, its dispatch line in `execute()` (or, for a
   request with a reply, in `executeRequest()` through one of the shared
   request shapes and a `Describe` for its results), and its line in
   `commandHelp()`. Named values go into `namedValue()` and `assertNames()`.
2. Cover the command in `src/tests/test_StudioTestClient.cpp`, which runs
   scripts against an in-process `StudioServer`.
3. Add `scenarios/<name>.studio` (self-contained, commented) and an
   `add_studio_scenario(<name>)` line in `CMakeLists.txt`; add the command and
   the scenario to the tables above.
