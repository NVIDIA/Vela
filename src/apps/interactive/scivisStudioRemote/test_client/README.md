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
string). Waiting commands take a trailing `timeout=MS` that overrides
`--timeout` for that command only.

## Commands

The vocabulary is the milestone-3 server surface. A command that the server
answers with `Error` fails the script unless it was written as an expectation
(`expect-error`).

| command | does |
|---------|------|
| `connect [HOST] [PORT]` | TCP connect, exchange Hellos (exact `PROTOCOL_VERSION` match), await the complete Bootstrap |
| `disconnect` | send Disconnect and close; mirror and replica are cleared -> Disconnected |
| `shutdown` | send Shutdown and await the server closing the socket -> Disconnected |
| `ping` | send Ping, await Pong |
| `await-lost` | wait until the connection is Lost (mirror and replica stay as a frozen view) |
| `reconnect` | connect again to the last host and port; a fresh handshake and Bootstrap |
| `sleep MS` | keep polling (events still print) for MS milliseconds |
| `expect-error [SUBSTRING]` | the next non-Frame server message must be an Error, containing SUBSTRING if given |
| `send-raw TYPE [HEX ...]` | send a message of type byte TYPE (0..255) with the given payload bytes, verbatim |
| `set-frame-config W H` | request a frame size, await the FrameConfig ack |
| `set-encodings NAME[,NAME]` | offer frame encodings, most preferred first (`raw`, `turbojpeg`; case-insensitive) |
| `start-rendering` | ask the server to stream frames |
| `stop-rendering` | pause the stream |
| `await-frame [COUNT]` | wait for COUNT (default 1) further frames |
| `save-frame PATH.ppm` | decode the newest frame into a binary P6 PPM (relative to the working directory) |
| `set-param TYPE INDEX NAME ANARITYPE VALUE...` | optimistic edit: set a parameter on the mirror and on the wire (`camera 1 fovy float32 0.9`, `camera 1 position float32_vec3 1 2 3`, `... note string "two words"`, `... flag bool true`) |
| `remove-param TYPE INDEX NAME` | remove a parameter, mirror and wire |
| `set-node-transform LAYER NODE M0..M15` | set a transform node's matrix, column-major; NODE is the server's node index |
| `dump-scene` | one `EVT Object` line per mirror object: type, index, subtype, name, params count |
| `dump-layers` | one `EVT Layer` line per mirror layer: index, name, nodes, active |
| `dump-project` | one `EVT Project` line from the replica: name, activeShot, shots, datasets, rigs, dirty |
| `dump-frame` | one `EVT Frame` line for the newest frame header |
| `assert VALUE OP RHS` | compare a named value; OP in `== != < <= > >= contains` |

Object TYPE and ANARITYPE are the ANARI names without the `ANARI_` prefix,
case-insensitive (`camera`, `light`, `float32`, `float32_vec3`, `int32`,
`bool`, `string`). Vector, matrix and box values take one token per
component.

### Assert values

Numbers compare numerically (at float32 precision for equality, so `== 0.9`
holds for a parameter set from `0.9`); everything else compares as text.

| value | is |
|-------|----|
| `state` | `NeverConnected`, `Connected`, `Lost` or `Disconnected` |
| `scene.objects`, `scene.layers`, `scene.cameras`, `scene.renderers` | counts in the Structural Mirror |
| `project.activeShot`, `project.shots`, `project.datasets` | Project Replica fields (FAIL before the first snapshot) |
| `frame.width`, `frame.height`, `frame.encoding`, `frame.shotId`, `frame.frame` | header of the newest frame (FAIL before the first frame); encodings read `Raw`, `TurboJpeg` |
| `frames.received` | frames consumed so far (frames superseded before they were read are not counted) |
| `frameConfig.width`, `frameConfig.height` | the last FrameConfig the server acknowledged |
| `param.<type>.<index>.<name>` | a mirror parameter's value: strings verbatim, bools `true`/`false`, numbers space-separated per component (`"1 2 3"`), object references `type:index`; a missing parameter is a FAIL |
| `errors.received`, `lastError` | Error messages received and the text of the newest one |

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

`scenarios/*.studio` are self-contained scripts for the milestone-3 server:
each starts with `connect` and ends with `disconnect` or `shutdown`, and
passes against `scivisStudioServer --library helide --data-root <tmp>` on a
fresh unsaved project.

| scenario | covers |
|----------|--------|
| `session.studio` | Bootstrap asserts, ping, disconnect, reconnect, shutdown |
| `frames_raw.studio` | Raw encoding, frame config ack, streaming, header asserts, `save-frame` |
| `frames_turbojpeg.studio` | the same with TurboJpeg; needs `VSR_USE_TURBOJPEG` at both ends |
| `scene_edits.studio` | `set-param`/`remove-param` on the active shot's camera, mirror asserts, frames still arrive |
| `errors.studio` | `send-raw` of unknown, unassigned and not-yet-implemented types, each answered with an Error |
| `loss.studio` | the server killed externally: `await-lost`, frozen mirror, `reconnect` to a restarted server |

### By hand

Start a server, then point the client at its port:

```bash
export LD_LIBRARY_PATH=<anari prefix>/lib:<libjpeg-turbo prefix>/lib
scivisStudioServer --library helide --data-root /tmp/studio-data --port 12345 &
scivisStudioTestClient --port 12345 --script test_client/scenarios/session.studio
scivisStudioTestClient --port 12345 -e "connect; start-rendering; await-frame 3; dump-frame; shutdown"
```

Or let `scenarios/run_scenario.sh` do it: it picks a free port, starts the
server with `--library helide --data-root <mktemp -d>` in a temporary
directory (where `save-frame` files land), waits for its `Listening on port`
line, runs the client with `--script`, propagates the client's exit code,
always stops the server, and prints both logs on failure. Extra arguments go
to the server after `--library helide --data-root <tmp>` (a `--project`, say);
do not pass `--port`, the runner owns it.

```bash
test_client/scenarios/run_scenario.sh build/scivisStudioServer \
  build/scivisStudioTestClient test_client/scenarios/frames_raw.studio
```

`loss.studio` starts with the hint `# runner: kill-restart-after 3`: once the
client has printed three `OK` records the runner kills the server (SIGKILL)
and starts a new one on the same port, which is what the script's
`await-lost` and `reconnect` need. Run without the runner, `await-lost`
FAILs after its deadline because nothing kills the server.

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
   `Command`-taking member, its dispatch line in `execute()`, and its line in
   `commandHelp()`. Named values go into `namedValue()` and `assertNames()`.
2. Cover the command in `src/tests/test_StudioTestClient.cpp`, which runs
   scripts against an in-process `StudioServer`.
3. Add `scenarios/<name>.studio` (self-contained, commented) and an
   `add_studio_scenario(<name>)` line in `CMakeLists.txt`; add the command and
   the scenario to the tables above.
