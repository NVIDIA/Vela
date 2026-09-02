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
└── client/     scivisStudioClient           ImGui UI over the client core
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
  project directory's parent becomes the root.
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

## What milestone 3 covers

Remote-viewer parity, item 3 of the spec's
[staged implementation plan](../../../../docs/scivis-studio-client-server.md#staged-implementation-plan):
Hello handshake with exact-match `PROTOCOL_VERSION`, the bracketed Bootstrap
(structural scene with descriptor-only arrays, layers, frame config, Project
Snapshot), raw and turbojpeg frames with header and encoding negotiation,
optimistic camera and parameter edits with origin-based echo suppression,
the render-loop Control-State Latch, Ping/Pong liveness, and the
`NeverConnected`/`Connected`/`Lost`/`Disconnected` client states.

Not there yet (milestones 4-6): Project Ops and the Project Replica-driven
editors, Remote Browse, Server Tasks, playback and time, picking, viewport
passes, `RenderShot`, and UI-state round-trips. The server answers those
messages with `Error{"... not implemented in this server"}`.

## Tests

`vsrTests "[StudioProtocol]"` (codecs), `"[StudioClient]"` (client core
against a fake server), `"[StudioServer]"` (server against a raw
`NetworkClient`) and `"[StudioRemote]"` (server and client core in one
process). The last two render with `helide` and skip when it cannot be
loaded.
