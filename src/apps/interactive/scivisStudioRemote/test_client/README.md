# scivisStudioTestClient

A complete Studio client with no UI: a command-line program that speaks the
Studio Message Set from a script so `scivisStudioServer` can be exercised end
to end on a machine with no display, in CI, or by an agent working
unattended. It exists so every server milestone can be verified without
launching the interactive client.

It is the GUI client's session, driven by a script: `TestSession` owns a
`client::ServerConnection` and records it. The connection is the one
`scivisStudioClient` uses -- connect, the Hello exchange, the Bootstrap into
a Structural Mirror and Project Replica, Ping/Pong liveness, loss detection,
Project Ops -- and `TestSession` adds what a script needs and a UI does not:
the event stream, the counters, and the replies, picks and task records kept
by request and task id. It records from `ServerConnection::onMessage`, which
fires for every message `poll()` consumed once that message has been handled,
so what an event reports is what the message left behind.

Two hand-written implementations of the client side were the earlier
arrangement, so that a bug in the GUI client's session code could not hide a
server bug. They were written from one template and stayed one file apart:
every protocol change landed twice, and milestone 7 paid for it three times.
The independence that matters is kept where it costs nothing: the server
suites in `src/tests/test_StudioServer*.cpp` drive the server with a raw
`TestClient` that shares no session code with either client, and this client
still holds its own Structural Mirror and Project Replica, so scripts assert
on what the server pushed, not just on which message types arrived.

Design: [`docs/scivis-studio-client-server.md`](../../../../../docs/scivis-studio-client-server.md),
section "Headless test client"; vocabulary: [`CONTEXT.md`](../CONTEXT.md).

Not a load or fuzz tool: one connection, sequential commands, no timing
measurement beyond deadlines.

## Command line

```
scivisStudioTestClient [--host H] [--port N] [--timeout MS] [--keep-going]
                       [--quiet-events] [--script FILE | -e "cmd; cmd" ...]
                       [--require-device] [--spawn-server SERVER [args...]]
scivisStudioTestClient --help | --markdown
```

| flag | meaning |
|------|---------|
| `--host H`, `--port N` | endpoint `connect` uses when the command names none (127.0.0.1:12345) |
| `--script FILE` | read commands from FILE, one per line |
| `-e "cmd; cmd"` | run these commands; repeatable, in order (cannot be combined with `--script`) |
| `--timeout MS` | deadline of every waiting command unless it carries `timeout=MS` (5000) |
| `--keep-going` | continue after a FAIL; the exit status is still non-zero |
| `--quiet-events` | print no `EVT` lines except those of the `dump-*` commands |
| `--spawn-server SERVER [args...]` | the rest of the line: start SERVER with those arguments plus `--port 0 --data-root <work>/data` in a fresh temporary working directory, wait for its `Listening on port` line, run the script against that port and stop it (see [Scenarios](#scenarios)); cannot be combined with `--port` |
| `--require-device` | with `--spawn-server`: exit 77 (ctest's skip) instead of 1 when the server loads no ANARI device |
| `--markdown` | print the [command table](#commands) as Markdown and exit |

Without `--script` or `-e` the script is read from stdin. Exit status: 0 iff
every command printed `OK`; 1 when any FAILed; 2 for a bad command line or a
script that does not parse; 77 under `--require-device` when the spawned
server loads no ANARI device. The record stream is stdout alone; transport
and session log lines go to stderr.

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
rendering and scene-edit commands, one request command per Project Op, Remote
Browse and task message, the playback, picking and viewport commands, the
offline render, and the waits that go with them.
The table below is generated from the runner's command table
(`scivisStudioTestClient --markdown` prints it; a test checks this file
carries it verbatim), so the names, arguments and one-line summaries here are
the ones `--help` and the `usage:` FAILs print. `<required>` and `[optional]`
arguments, `...` for a list; a `kind` of `request` means the command sends
one request and awaits its reply (`sync` when the reply ends the op, `task`
when it launches a Server Task), `wait` a wait on a task or a reply.

An `Error` the server sends while any command but `expect-error` runs FAILs
that command with the Error's text (only Project Ops carry request ids, so for
everything else the command in flight is the attribution); a waiting command
also FAILs as soon as the connection is Lost, naming the loss, rather than at
its deadline. Write the Errors a script provokes as expectations (`send-raw
60` then `expect-error`), and let the reply of a fire-and-forget command
settle (`sleep`) before the next one if its Error is to land on it.

The prefixes: `expect-fail <command>` makes the refused outcome the OK one
(`ok=false` for a request, `TaskFailed` for `await-task`), and applies to
`request` and `wait` commands; `no-wait <request>` sends without awaiting the
reply and applies to `request` commands only. Either on a `session` command
is a FAIL. A command that takes no argument FAILs when given one, and every
argument count outside the usage FAILs as `usage: <command> <usage>` before
anything reaches the wire.

| command | kind | does |
|---------|------|------|
| `add-light <lightRigId> [subtype]` | request | sync; adds a light to the rig (SUBTYPE defaults to directional); the reply carries lightNode=LAYER:NODE |
| `assert <value> <op> <rhs>` | session | compare a named value (the assert values below); OP in == != < <= > >= contains; RHS a literal, or @NAME for another named value |
| `await-frame [count]` | session | wait for COUNT (default 1) further frames |
| `await-frame-advance [count]` | session | wait for COUNT (default 1) further frames whose header frame differs from the previous frame's |
| `await-frame-at <frame>` | session | wait for a Frame whose header says frame == FRAME; the headers meanwhile print as they come |
| `await-lost` | session | wait until the connection is Lost (mirror and replica stay as a frozen view) |
| `await-reply [requestId]` | wait | collect the reply of a no-wait request, the oldest pending by default |
| `await-server` | session | wait until the server kill-server started reaches Listening (the session keeps polling meanwhile); needs --spawn-server |
| `await-snapshot` | session | wait for a ProjectSnapshot newer than the last thing awaited (the last request's reply, the last await-task's end, or the previous await-snapshot); each await consumes one snapshot |
| `await-task [taskId]` | wait | wait for the task (default $lastTaskId) to end; FAIL on TaskFailed, or under expect-fail on TaskCompleted |
| `await-task-progress [taskId]` | session | wait until the task (default $lastTaskId) has reported progress at least once (a report an earlier command printed counts); FAIL at once when it ends without any |
| `await-warning` | session | wait for the next TimeAdvanceWarning (a frame that failed to load while playing) |
| `cancel-task <taskId>` | request | sync; removes a queued task (it then ends as TaskFailed "cancelled") or asks a running render to stop at its next frame; a finished task is an error reply |
| `clone-light-rig <id>` | request | sync; lightRigId= |
| `connect [host] [port]` | session | TCP connect, exchange Hellos (exact PROTOCOL_VERSION match), await the complete Bootstrap |
| `copy-fixture <file>` | session | copy FILE (relative to the script's directory) into the spawned server's data root, so a script can import $dataRoot/<basename> without writing anything itself; needs --spawn-server |
| `create-camera-rig [name]` | request | sync; cameraRigId= |
| `create-color-map [name]` | request | sync; colorMapId= and object=TYPE:INDEX, the record and its scene-side Array |
| `create-light-rig [name]` | request | sync; lightRigId= |
| `create-shot [name]` | request | sync; shotId=; the new shot becomes active |
| `declare-file-animation-dataset <name> <importer> <source>... [set-frame-count=BOOL]` | request | sync; declares a file animation without importing it; the reply carries datasetId= |
| `disconnect` | session | send Disconnect and close; mirror and replica are cleared -> Disconnected |
| `discover-dataset-candidates` | request | sync; one EVT DatasetCandidate file= proposedName= per candidate |
| `dump-frame` | session | one EVT Frame line for the newest frame header |
| `dump-layers` | session | one EVT Layer line per mirror layer: index, name, nodes, active |
| `dump-project` | session | one EVT Project line from the replica (name, activeShot, counts, dirty, directory), then one EVT Shot, Dataset, LightRig, CameraRig and ColorMap line per entity |
| `dump-scene` | session | one EVT Object line per mirror object: type, index, subtype, name, params count |
| `expect-error [substring]` | session | the next server message other than a Frame or a liveness Pong must be an Error, containing SUBSTRING if given |
| `expect-pong` | session | the next non-Frame server message must be a Pong |
| `find-object <type> [first\|name=<name>]` | session | the first mirror object of that type, or the first one so named: one EVT Object line, and $lastObjectRef (type:index), $lastObjectType, $lastObjectIndex |
| `import-file-animation-dataset <name> <importer> <path>... [set-frame-count=BOOL]` | request | task: import a file animation from those files |
| `import-static-dataset <path> [name] [importer\|VSR_SUBTREE]` | request | task; IMPORTER is a vsr::io::ImporterType name (OBJ, PLY, ...) or VSR_SUBTREE for a subtree archive |
| `incorporate-dataset-candidate <file> [proposedName] [name]` | request | task: import a candidate discover-dataset-candidates found |
| `kill-server` | session | SIGKILL the spawned server (the process went away, no farewell) and start a replacement on the same port without waiting for it; needs --spawn-server |
| `list-directory <directory>` | request | one EVT DirectoryEntry name= kind= size= mtime= per entry (File, Directory, ProjectDirectory); refused outside every Data Root |
| `list-roots` | request | one EVT DataRoot path= per Data Root; the first is $dataRoot |
| `load-camera-rig-archive <file>` | request | sync; cameraRigId= |
| `load-dataset <id>` | request | task: load an unloaded dataset |
| `load-dataset-archive <file> [name]` | request | task: import a Dataset Archive; the dataset takes NAME, or the archive's own when none is given |
| `load-light-rig-archive <file>` | request | sync; lightRigId= |
| `new-project` | request | sync: an unsaved empty project replaces the current one |
| `open-project <directory>` | request | task: open the project stored in DIR |
| `pick <x> <y>` | session | send a Pick at that pixel (x right, y down from the top-left) and await its PickReply; sets $lastPickType and $lastPickIndex on a hit, unsets them on a miss |
| `ping` | session | send Ping; the Pong is for expect-pong |
| `reconnect` | session | connect again to the last host and port, retrying refused attempts until the deadline; a fresh handshake and Bootstrap |
| `refresh-dataset-availability <id>` | request | sync; checks again whether the dataset's source is there |
| `reimport-dataset <id>` | request | task: import the dataset again from its source |
| `remove-camera-rig <id>` | request | sync |
| `remove-color-map <id>` | request | sync |
| `remove-dataset <id> [keep-asset-file]` | request | sync; removes the saved asset file too unless told to keep it |
| `remove-light <lightRigId> <layer> <nodeIndex>` | request | sync; the node reference add-light returned |
| `remove-light-rig <id>` | request | sync |
| `remove-param <type> <index> <name>` | session | remove a parameter, mirror and wire |
| `remove-shot <id>` | request | sync |
| `rename-camera-rig <id> <newName>` | request | sync |
| `rename-color-map <id> <newName>` | request | sync |
| `rename-dataset <id> <newName>` | request | sync |
| `rename-light-rig <id> <newName>` | request | sync |
| `render-shot <shotId\|active>` | request | task: render the shot's frames offline; needs a saved project, refused while another render is queued or running; the end carries framesCompleted= and the output directory as its message |
| `request-array-histogram <type> <index> <bins> (or <type:index> <bins>)` | request | sync: bin a scalar array on the server; the reply prints bins= min= max= nonFinite= and fills the histogram.* values (cleared when the request goes out) |
| `save-camera-rig-archive <id> <file>` | request | sync |
| `save-dataset-archive <id> <file>` | request | task: write the dataset as a Dataset Archive |
| `save-frame <path.ppm>` | session | decode the newest frame into a binary P6 PPM (relative to the working directory) |
| `save-light-rig-archive <id> <file>` | request | sync |
| `save-project [directory]` | request | task: save to DIR, or to the project's own directory |
| `send-raw <typeByte 0..255> [hex bytes...]` | session | send a message of that type byte with the given payload bytes, verbatim |
| `set-active-shot <id>` | request | sync |
| `set-encodings <name>[,<name>...]` | session | offer frame encodings, most preferred first (raw, turbojpeg; case-insensitive) |
| `set-frame-config <width> <height>` | session | request a frame size, await the FrameConfig ack |
| `set-node-transform <layer> <node> <16 floats>` | session | set a transform node's matrix, column-major; NODE is the server's node index |
| `set-outline [<type> <index> \| <type:index> \| none]` | session | outline that object, or clear the outline (none or no argument) |
| `set-param <type> <index> <name> <anariType> <value...>` | session | optimistic edit: set a parameter on the mirror and on the wire (camera 1 fovy float32 0.9) |
| `set-playing <shotId\|active> on\|off` | request | sync: start or stop playback of the active shot (another id is refused); the reply and a snapshot follow |
| `set-time <shotId\|active> <frame>` | session | one-way scrub (latest-wins); while paused the server commits Time at Rest with one debounced snapshot; a SHOT that is not active is ignored silently |
| `shutdown` | session | send Shutdown and await the server closing the socket -> Disconnected |
| `sleep <ms>` | session | keep polling (events still print) for MS milliseconds; a loss meanwhile is the next command's to notice |
| `start-rendering` | session | ask the server to stream frames |
| `stop-rendering` | session | pause the stream |
| `unload-dataset <id>` | request | sync |
| `update-shot <id> <field>=<value>...` | request | sync: a patch of the named fields alone (the id must be in the replica); fields name, frameCount, fps, loop, currentFrame, lightRigId, cameraRigId, renderSettings.*, binding.<datasetId>=on\|off |
| `viewport-settings <key>=<value>...` | session | edit the remembered ViewportSettings and send the whole struct (unset keys keep their last value); keys highlightSelection, outlinePrimitives, showWorldBounds, edgeInvert, worldBoundsColor=r,g,b,a, worldBoundsWidth, visualizeAOV, depthVisualMinimum, depthVisualMaximum |

### Session, rendering and scene edits

Object TYPE and ANARITYPE are the ANARI names without the `ANARI_` prefix,
case-insensitive (`camera`, `light`, `float32`, `float32_vec3`, `int32`,
`bool`, `string`). Vector, matrix and box values take one token per
component: `set-param camera 1 fovy float32 0.9`, `set-param camera 1
position float32_vec3 1 2 3`, `... note string "two words"`, `... flag bool
true`. Where a command takes an object as `TYPE INDEX`, the one-token
`TYPE:INDEX` spelling `$lastObjectRef` expands to is accepted as well
(`set-outline`, `request-array-histogram`). `find-object` looks every array
kind up in the mirror's one `array` pool. `reconnect` retries refused
attempts until its deadline, since a server being restarted refuses at
once. `sleep`'s MS, like every
`timeout=MS`, must fit a deadline. `save-frame` writes relative to the
working directory. `copy-fixture`, `kill-server` and `await-server` act on
the server `--spawn-server` started and FAIL without one: `copy-fixture`
resolves a relative FILE against the script's directory (the working
directory for `-e`) and puts it under the spawned server's data root;
`kill-server` is the "process went away" case, a SIGKILL with no farewell,
after which a replacement is started on the same port and `await-server`
waits for its `Listening` line, so a loss script needs no guessed sleep.

### Playback, picking and the viewport

Time in motion arrives in the Frame headers; Time at Rest in the Project
Snapshot's active shot (`await-snapshot` prints its `playing=` and
`currentFrame=`). `SHOT` is a shot id or `active`, the replica's active
shot. `set-time` is a one-way scrub (latest-wins): while paused, the server
commits Time at Rest with one debounced snapshot once no `SetTime` has
arrived for 250 ms and the frame changed; a `SHOT` that is not active is
ignored by the server, silently. `await-warning` prints the warning as `EVT
TimeAdvanceWarning shotId= frame= message=`.

`pick X Y` picks in frame-header pixels (x right, y down from the top-left;
the server clamps what lies outside the frame) and prints the reply as `EVT
PickReply requestId= hit= worldPosition="x y z" objectType= objectIndex=`
(`none` when nothing was hit). It is request/reply like a Project Op but its
reply is a plain `PickReply`, not a `ProjectOpReply`: `expect-fail` and
`no-wait` do not apply to it. `set-time`, `set-outline` and
`viewport-settings` are optimistic: they send and print whatever events were
queued.

`viewport-settings` keys: `highlightSelection`, `outlinePrimitives`,
`showWorldBounds`, `edgeInvert` (bools), `worldBoundsColor=r,g,b,a`,
`worldBoundsWidth`, `visualizeAOV` (`NONE`, `DEPTH`, `ALBEDO`, `NORMAL`,
`EDGES`, `OBJECT_ID`, `PRIMITIVE_ID`, `INSTANCE_ID`), `depthVisualMinimum`,
`depthVisualMaximum`; an unknown key is a usage error. Unset keys keep the
last value sent (the struct's defaults at first), so repeated commands
compose; with no edits the current struct is sent again.

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

`update-shot ID FIELD=VALUE...` sends an `UpdateShot` whose patch carries
exactly the named fields (the id must name a shot of the replica; every
other field of the server's Shot stands); the fields are `name`,
`frameCount`, `fps`, `loop`,
`currentFrame`, `lightRigId`, `cameraRigId`,
`renderSettings.{width,height,samples,rendererLibrary,rendererSubtype,rendererObjectIndex,outputFilePrefix}`
and `binding.<datasetId>=on|off` (`playing` is refused: it belongs to
playback). `set-playing` starts or stops playback of the active shot only
(another id is refused); the reply and a snapshot follow.
`request-array-histogram` clears the `histogram.*` values when the request
goes out, so a refused one leaves none. `list-directory` prints one entry per
line (`File`, `Directory`, `ProjectDirectory`) and FAILs (or `expect-fail`s)
outside every Data Root.

Task-launching ops answer at once with `taskId=` and run on the server's loop
one at a time; `await-task` waits for the `TaskCompleted`/`TaskFailed`, the
`TaskProgress` lines printing as they come (`current= total=`, determinate
for a render, `total=0` otherwise), and the end line carrying
`framesCompleted=` when the task wrote frames. The completion message lands
in `$lastTaskMessage`: an import's is the new dataset's id (then also
`$lastDatasetId`), a render's the output directory. A task that fails FAILs
the `await-task`, unless it is written `expect-fail await-task [TASKID]`,
when a completion FAILs instead. `await-task-progress` waits until the task
has reported progress at least once (a report an earlier command already
printed counts) and FAILs at once when it ends without any.

`render-shot SHOT` needs a saved project and is refused while another render
is queued or running. The shot becomes active first (a snapshot precedes the
progress), the progress is determinate (`current=<frame> total=<frames>`),
the end carries `framesCompleted=` and, when completed, the output directory
as its message. While it is queued or running, mutating requests that reach
dispatch are refused with "render in progress" and interactive frames pause;
a request that arrives while the render body holds the loop is latched and
dispatched once it returns, so it is refused only when another render is
queued by then. `cancel-task TASKID` removes a queued task (it then ends as
`TaskFailed "cancelled"`), or asks a running render to stop at its next frame
(the reply is ok once it has; the task ends as `TaskFailed "cancelled"` with
`framesCompleted=` the frames left on disk); a finished task is an error
reply.

`await-snapshot` waits for a `ProjectSnapshot` newer than the last thing
awaited: the reply to the last request command, the end of the last
`await-task`, or the previous `await-snapshot`. Each `await-snapshot`
consumes one snapshot, so two that land together (a `set-playing`'s and the
auto-stop's) are awaited one at a time. A snapshot that arrived before the
mark belongs to something earlier, so await a task that sends none (a
cancelled one) before the one whose snapshot is wanted.

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

### Variables

| variable | set by |
|----------|--------|
| `$lastShotId`, `$lastLightRigId`, `$lastCameraRigId`, `$lastColorMapId` | the `*CreatedResult` of the create, clone and archive-load replies |
| `$lastObjectRef` (`type:index`), `$lastObjectType`, `$lastObjectIndex` | `create-color-map`'s scene-side object, and `find-object` |
| `$lastPickType`, `$lastPickIndex` | the identity a `pick` hit (unset again by a miss) |
| `$lastLightLayer`, `$lastLightNode` | `add-light` |
| `$lastDatasetId` | `declare-file-animation-dataset`'s reply, and the completion message of an `import-static-dataset`, `import-file-animation-dataset`, `load-dataset-archive` or `incorporate-dataset-candidate` task after `await-task` (the runner remembers which command launched the task; the message is not inspected) |
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
| `scene.objects` | objects of every type in the Structural Mirror |
| `scene.layers` | layers in the mirror |
| `scene.cameras` | cameras in the mirror |
| `scene.renderers` | renderers in the mirror |
| `project.<field>` | a Project Replica field (FAIL before the first snapshot): `name`, `activeShot`, `shots`, `datasets`, `lightRigs`, `cameraRigs`, `colorMaps`, `dirty`, `directory`; the collections are their sizes |
| `shot.<id>.<field>` | a Shot in the replica (`active` names the active shot; an unknown id is a FAIL): `name`, `frameCount`, `fps`, `currentFrame`, `loop`, `playing`, `lightRigId`, `cameraRigId`, `bindings`, `camera`, `renderSettings.width`, `renderSettings.height`, `renderSettings.samples`, `renderSettings.rendererLibrary`, `renderSettings.rendererSubtype`, `renderSettings.rendererObjectIndex`, `renderSettings.outputFilePrefix`, `binding.<datasetId>` (`true`/`false`; FAIL when unbound); `camera` reads `type:index`, `bindings` is a count |
| `dataset.<id>.<field>` | a Dataset in the replica (an unknown id is a FAIL): `name`, `status`, `residency`, `sourceKind`, `importerType`, `sourcePath`, `rootNode`, `dirty`, `declared`; `status` reads `Available`, `Unavailable`, `Importing` or `ImportFailed`, `residency` `Loaded` or `Unloaded`, `rootNode` `layer:node` |
| `lightRig.<id>.<field>` | a LightRig in the replica: `name`, `rootNode` |
| `cameraRig.<id>.<field>` | a CameraRig in the replica: `name`, `keyframes`; `keyframes` is a count |
| `colorMap.<id>.<field>` | a ColorMap record in the replica: `name` |
| `tasks.completed` | `TaskCompleted` messages received since the session object was made, across reconnects, the ones a Bootstrap replays included (so an end heard live and then replayed counts twice). The "connection lost" failures a `BootstrapBegin` declares are not messages and do not count |
| `tasks.failed` | `TaskFailed` messages received, counted like `tasks.completed` |
| `tasks.replayed` | task messages (progress or end) the newest Bootstrap carried between its Begin and End: the server's task-status replay of what ended since the previous Bootstrap, and the running task's status. 0 again at every `BootstrapBegin` |
| `task.<id>.<field>` | a task record (`last` is `$lastTaskId`; FAIL when nothing has been heard of the id): `state`, `message`, `framesCompleted`, `current`, `total`; `state` is `Queued` from the launching reply, `Running` from the first `TaskProgress`, then `Completed` or `Failed`; `message` the completion message or the failure's error; `current` and `total` the newest progress (0 when indeterminate) |
| `replies.failed` | replies with `ok=false` |
| `replies.pending` | `no-wait` requests awaiting collection |
| `snapshots.received` | Project Snapshots applied, the Bootstrap's included |
| `browse.entries` | entries of the last `list-directory` (0 after a refused one) |
| `var.<name>` | a variable's value (see [Variables](#variables)); `$name` in this position would expand to a value name |
| `frame.<field>` | the header of the newest frame (FAIL before the first frame): `width`, `height`, `encoding`, `shotId`, `frame`; encodings read `Raw`, `TurboJpeg` |
| `frames.received` | frames consumed so far (frames superseded before they were read are not counted) |
| `frames.advanced` | consumed frames whose header `frame` differed from the previous one's. A step backwards (a loop wrap to 0, a scrub back) is time moving on purpose, not a skip, and does not count; a forward scrub does |
| `frames.maxStep` | the largest forward step between two consecutive headers. Frames are latest-wins, so a client slower than the stream can see steps the server never took |
| `frameConfig.width` | the width of the last FrameConfig the server acknowledged |
| `frameConfig.height` | the height of the last FrameConfig the server acknowledged |
| `param.<type>.<index>.<name>` | a mirror parameter's value: strings verbatim, bools `true`/`false`, numbers space-separated per component (`"1 2 3"`), object references `type:index`; a missing parameter is a FAIL |
| `errors.received` | Error messages received |
| `lastError` | the text of the newest Error |
| `lastReplyError` | the error text of the newest failed reply |
| `warnings.received` | `TimeAdvanceWarning`s received |
| `lastWarning` | the message of the newest `TimeAdvanceWarning` (empty before one) |
| `pick.<field>` | the last `PickReply` (FAIL before one): `hit`, `worldPosition`, `objectType`, `objectIndex`; `hit` `true`/`false`, `worldPosition` `"x y z"`, and the identity (`surface`/`volume` and the pool index, or `none`) |
| `histogram.<field>` | the last ok `request-array-histogram` (FAIL when the last request was refused or none was made): `bins`, `min`, `max`, `total`, `nonFinite`; `bins` the bin count, `min` and `max` the value range, `total` the sum of all bins, `nonFinite` the NaN/inf elements left out of them |

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
and a record of the old process with the same id starts over (Queued, no
progress, no message) the moment the new task's launch reply or first
`TaskProgress` names it, so `await-task` waits for the new task's end.

## The record stream

Every line is one record: exactly one `OK <command>` or `FAIL <command>:
<reason>` per command, and one `EVT <Name> key=value ...` per server message
as the session consumes it (Frames are latest-wins, so a fast stream shows
fewer `EVT Frame` lines than the server sent). `<command>` is the command as
written, so the output can be grepped for the script's own lines. A message
that could not be decoded, and a scene push the mirror refused as sent, carry
`malformed=true` instead of (or beside) the fields they would have had.

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
| `loss.studio` | the server killed externally (`kill-server`): `await-lost`, frozen mirror, `await-server`, `reconnect` to the restarted server |
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
| `loss_during_task.studio` | the server killed (`kill-server`) while a 400-frame render of this client runs: Lost keeps the record `Running`, the reconnect's Bootstrap fails it with `connection lost`, the restarted server replays nothing |

### By hand

Start a server, then point the client at its port:

```bash
export LD_LIBRARY_PATH=<anari prefix>/lib:<libjpeg-turbo prefix>/lib
scivisStudioServer --library helide --data-root /tmp/studio-data --port 12345 &
scivisStudioTestClient --port 12345 --script test_client/scenarios/session.studio
scivisStudioTestClient --port 12345 -e "connect; start-rendering; await-frame 3; dump-frame; shutdown"
```

Or let the client start the server itself with `--spawn-server`, which takes
the rest of the command line as the server binary and its arguments:

```bash
scivisStudioTestClient --script test_client/scenarios/frames_raw.studio \
  --spawn-server build/scivisStudioServer --library helide
```

The client makes a temporary working directory (`vsrStudioScenario-<name>-*`
under the system temp dir) holding the server's data root (`data/`) and its
log (`server.log`, stdout and stderr of every server lifetime), starts the
server there with `--port 0 --data-root <work>/data` appended to the given
arguments, waits up to 30 s for its `Listening on port N` line and connects
to that `N`, so no port is picked ahead of the bind and two runs never
collide. The script runs from the working directory (where `save-frame`
files land), the server is stopped afterwards (SIGTERM, then SIGKILL), and
the directory is removed on success or kept on failure, its path and the
server log on stderr for a post-mortem. Do not pass `--port` or `--data-root`
to the server; the client owns them.

`datasets.studio`, `tasks.studio`, `pick.studio`, `histogram.studio` and the
render scenarios open with `copy-fixture fixtures/triangle.obj`, which copies
the file (relative to the scenario) into the data root, so the script imports
`$dataRoot/triangle.obj` without writing anything itself.

`loss.studio` and `loss_during_task.studio` script the server's loss:
`kill-server` SIGKILLs the spawned server and starts a fresh one on the same
port without waiting, `await-lost` sees the socket close, `await-server
timeout=30000` waits for the replacement's `Listening` line and `reconnect`
then finds it at once. Against a server started by hand these three commands
FAIL, since there is nothing of the client's to kill.

### Under ctest

`test_client/CMakeLists.txt` registers one `vsr::StudioScenario.<name>` test
per scenario when `BUILD_TESTING` is on (`frames_turbojpeg` only with
`VSR_USE_TURBOJPEG`), each running the client with `--script`,
`--require-device` and `--spawn-server <scivisStudioServer> --library
helide`, with a 120 s timeout. The client exits 77 when the server can load
no ANARI device, which ctest reports as a skip (`SKIP_RETURN_CODE 77`)
rather than a failure.

```bash
LD_LIBRARY_PATH=<anari prefix>/lib ctest --test-dir build -R StudioScenario -j 8
```

## Adding commands and scenarios

Each later milestone lands with the test client commands and scenarios that
exercise its server surface, so the scenarios stay the acceptance test of the
whole protocol:

1. Add the session-level operation to `TestSession` (a send, plus the
   `Event` its answer is recorded as in `TestSession::record()`; the
   handling itself belongs to `client::ServerConnection`, which both clients
   share), then one row to the command table in
   `CommandRunner.cpp` (`commands()`, sorted by name): the name, usage,
   arity, kind and handler, and a one-line summary. The handler is a member
   in the file of its area (`SessionCommands.cpp`, `SceneCommands.cpp`,
   `RequestCommands.cpp`, `WaitCommands.cpp`) or, for a request of a shape
   the table already knows (`idRequest<R>`, `nameRequest<R>`, ...), the
   shape bound to the request type and a `Describe` for its results. A named
   value is one row of the value table in `NamedValues.cpp`
   (`namedValues()`: name or pattern, summary, resolver); a field of a
   replica record is one row of its table in `RecordFields.cpp`, which
   `assert`, `dump-project` and `update-shot` all read. Regenerate the
   command table and the assert-value table above with
   `scivisStudioTestClient --markdown`; the `[StudioTestClient]` suite checks
   this file carries both.
2. Cover the command in the `[StudioTestClient]` suite:
   `src/tests/test_StudioTestClientServer.cpp` runs scripts against an
   in-process `StudioServer`; `test_StudioTestClientProjectOps.cpp` drives
   the `FakeProjectServer` for what a real server cannot produce;
   `test_StudioTestClientScript.cpp` holds the parser, option and command
   table checks.
3. Add `scenarios/<name>.studio` (self-contained, commented; `copy-fixture`
   first when it imports a fixture) and an `add_studio_scenario(<name>)` line
   in `CMakeLists.txt`; add the command and the scenario to the tables above.
