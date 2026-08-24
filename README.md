Vela
====

Vela is a C++17 project built around **VSR** ("Vela Scene Runtime"), a scene
graph library designed to pair a live, editable scene description with one or
more [ANARI](https://www.khronos.org/registry/ANARI/) devices. Applications
build a scene once in VSR and render it through any ANARI implementation,
which makes VSR useful both as a scene-authoring layer and as a way to compare
device behavior side by side.

This repository contains:

- [The VSR library collection](src/vsr) (core utilities, scene, I/O, rendering, app glue, UI, scripting)
- [ANARI device implementation backed by VSR](src/anari_vsr) for capturing ANARI state from other applications
- [Interactive ImGui-based viewer](src/apps/interactive/viewer/)
- [SciVis Studio](src/apps/interactive/scivisStudio/), a project/shot-oriented scientific visualization application
- [Additional interactive apps](src/apps/interactive/): multi-device viewer, data tree editor, networked client/server, and an experimental MPI distributed viewer
- [Focused interactive demos](src/apps/interactive/demos/)
- [Single-file tutorials showing specific concepts](src/apps/tutorial/)
- [Command-line tools](src/apps/tools/) for conversion, inspection, headless rendering, and scripting
- [Unit tests](src/tests/)

VSR's scene library (`vsr_scene`) generally follows 1:1 with ANARI's object
hierarchy, defined in the ANARI specification
[here](https://registry.khronos.org/ANARI/specs/1.1/ANARI-1.1.html).

Vela is tested on Linux, but is also intended to be usable on other operating
systems such as macOS and Windows.

Note that Vela is experimental and does not yet track a versioning system, so
there are no API or ABI stability guarantees.

## Building from source

The repository uses CMake 3.21+ and requires the
[ANARI-SDK](https://github.com/KhronosGroup/ANARI-SDK) 0.15.0 or newer to be
findable via `find_package(anari)`. Builds must happen in a directory separate
from the source directory:

```bash
% cd /path/to/vela
% mkdir build
% cd build
% cmake -DCMAKE_BUILD_TYPE=Release ..
% cmake --build . --parallel
```

Using a tool like `ccmake` or `cmake-gui` will let you see which options are
available to enable. The most commonly used ones are:

- `VSR_BUILD_APPS`             : build the applications and command-line tools
- `VSR_BUILD_INTERACTIVE_APPS` : build the windowed applications
- `VSR_BUILD_UI_LIBRARY`       : build the ImGui-based UI library
- `VSR_USE_CUDA`               : enable CUDA code paths where relevant
- `VSR_USE_LUA`                : enable Lua scripting support
- `VSR_USE_MPI`                : enable MPI support
- `VSR_USE_NETWORKING`         : enable networking support via Boost.Asio

Many file importers carry additional build dependencies and are therefore
off by default, each behind its own option: `VSR_USE_ASSIMP`, `VSR_USE_HDF5`,
`VSR_USE_OIIO`, `VSR_USE_SILO`, `VSR_USE_TORCH`, `VSR_USE_USD`, and
`VSR_USE_VTK`. If an expected format is missing at runtime, check whether its
option was enabled in your build.

## Running the applications

The best starting point for understanding how the libraries compose is
[`src/apps/tutorial/`](src/apps/tutorial/), which holds single-file examples
covering rendering, image pipelines, the `Forest` container, `DataTree`
serialization, and USD export.

### vsrViewer

[`vsrViewer`](src/apps/interactive/viewer/) is the main interactive
application. It loads scene data from the supported file formats, lets you
inspect and edit objects, layers, parameters, animations, and transfer
functions, and can save and reload full sessions as `.vsr` files.

The list of devices offered in the UI comes from a comma-separated list in the
`VSR_ANARI_LIBRARIES` environment variable. If it is unset the app falls back
to a small set of defaults, but defining it in your environment (e.g. in a
`.bashrc`) gives you control over what appears:

```bash
% export VSR_ANARI_LIBRARIES=helide,visrtx
% ./vsrViewer
```

Files can be loaded on the command line using a pattern of:

```bash
% ./vsrViewer -[loader type] [file1] [file2...]
```

Once a loader type is selected, every filename after it uses that loader until
the next loader type appears. Common loader types are `assimp`, `dlaf`, `hdri`
(environment light), `obj`, and `ply`. See [vsr_io](src/vsr/io/) for the full
set of supported formats, and
[`vsr::app::Context::parseCommandLine()`](src/vsr/app/Context.cpp) for the
definitive list of available options.

### Command-line tools

[`src/apps/tools/`](src/apps/tools/) contains headless utilities, including
`vsrOffline` and `vsrRender` for offline rendering, `vsrPrint` for inspecting
serialized `.vsr` files, `vsrVolumeToNanoVDB` for volume conversion, and
`vsrLua` for scripted workflows.

### anari_vsr

[`anari_vsr`](src/anari_vsr/) is an ANARI device that mirrors ANARI object
state into a VSR scene and writes a `live_capture.vsr` archive as frames are
committed. Point an existing ANARI application at it to capture what that
application actually submits, then open the result in `vsrViewer`. It can also
forward rendering to a real backend device, selected with the
`ANARI_VSR_LIBRARY` environment variable (`helide` by default).

## Scripting

With `VSR_USE_LUA=ON`, scenes can be authored and automated from Lua. `vsrLua`
is a standalone interpreter and REPL, while `vsrViewer` embeds a Lua terminal
and builds its Actions menu from Lua modules. Both expose a pre-bound `scene`
variable. See [src/vsr/scripting/](src/vsr/scripting/) for the API and
[`scripts/examples/`](scripts/examples/) for worked examples.

## Documentation

- [src/vsr/README.md](src/vsr/README.md) — index of the libraries and what each one covers
- [AGENTS.md](AGENTS.md) — architecture overview, dependency layers, and key design patterns
- [CONTEXT-MAP.md](CONTEXT-MAP.md) — how the project's domain contexts relate, with links to per-area glossaries
- [docs/adr/](docs/adr/README.md) — architecture decision records
- [STYLEGUIDE.md](STYLEGUIDE.md) — C++ coding conventions used throughout the project

## Licensing

Vela is licensed under the Apache License 2.0 — see [LICENSE](LICENSE).
Contributions are accepted under the Developer Certificate of Origin; see
[CONTRIBUTING.md](CONTRIBUTING.md).

Third-party components vendored under [`external/`](external/) remain under
their own licenses, listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Three of them preserve their
upstream license text in place:

- `external/vela_fmtlib` — {fmt}, MIT License
  ([external/vela_fmtlib/LICENSE](external/vela_fmtlib/LICENSE)).
- `external/vela_glm` — OpenGL Mathematics (GLM), dual-licensed under
  **The Happy Bunny License (Modified MIT License) OR the MIT License**, at your
  option ([external/vela_glm/LICENSE](external/vela_glm/LICENSE), which contains
  the full text of both). The Happy Bunny License is the MIT License plus a
  non-binding restriction regarding military use; the plain MIT option is
  available without that restriction.
- `external/vela_agx` — agx (Animated Geometry eXchange), an independently
  published third-party project by Jefferson Amstutz
  ([upstream](https://github.com/jeffamstutz/agx)), Apache License 2.0
  ([external/vela_agx/LICENSE](external/vela_agx/LICENSE)).
