# AGENTS.md

This file provides guidance for coding agents working in this repository.

## About Vela

Vela is a C++17 project built around VSR ("Vela Scene Runtime"), a scene graph library that pairs a live, editable scene description with one or more ANARI devices. It is **experimental** and has no API stability guarantees.

See [CODING_STANDARDS.md](CODING_STANDARDS.md) for VSR-specific and project-wide C++ coding
conventions, and [CONTEXT-MAP.md](CONTEXT-MAP.md) for how the domain contexts
below relate to each other.

## Domain Docs

Each area keeps a `CONTEXT.md` glossary. Use these terms in code and docs:

- [src/vsr/core/CONTEXT.md](src/vsr/core/CONTEXT.md) — Data Trees, Data Paths, and change-notification vocabulary
- [src/vsr/io/CONTEXT.md](src/vsr/io/CONTEXT.md) — Archives, serialization verbs, import/export vocabulary
- [src/vsr/app/CONTEXT.md](src/vsr/app/CONTEXT.md) — Application Dumps and app-level state
- [src/vsr/rendering/CONTEXT.md](src/vsr/rendering/CONTEXT.md) — Image Pipeline / Image Pass / render-index vocabulary
- [src/apps/interactive/scivisStudio/CONTEXT.md](src/apps/interactive/scivisStudio/CONTEXT.md) — SciVis Studio Projects and shots

Decisions that are expensive to rediscover are recorded as ADRs under
[docs/adr/](docs/adr/README.md). Read the relevant ones before changing project
layout, archive schemas, image orientation, or USD import behavior.

## Build

From the repo root:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel
```

Requires ANARI-SDK 0.15.0+ (`find_package(anari)` must succeed).

Key optional CMake flags: `VSR_BUILD_APPS`, `VSR_BUILD_INTERACTIVE_APPS`,
`VSR_BUILD_UI_LIBRARY`, `VSR_USE_LUA`, `VSR_USE_ASSIMP`, `VSR_USE_HDF5`, `VSR_USE_MPI`, `VSR_USE_NETWORKING`, `VSR_USE_TURBOJPEG` (needs networking; JPEG-compressed SciVis Studio remote frames), `VSR_USE_VTK`, `VSR_USE_SILO`, `VSR_USE_USD`, `VSR_USE_OIIO`, `VSR_USE_MINISCENE` (fetches miniScene; enables `export_SceneToMiniScene`).

## Tests

Test sources: `src/tests/` (one `test_*.cpp` per suite). Run via ctest from the build directory, e.g. `ctest -C Release -R test_Forest`.

## Architecture

### Library Dependency Layers

```text
vsr_core  ->  vsr_scene  ->  vsr_io  ->  vsr_rendering  ->  vsr_app
                                  \                  /
                               (optional: vsr_ui_imgui, vsr_mpi, vsr_network, vsr_lua)
```

- **`vsr_core`** (`src/vsr/core/`): `Any`, `Token`, `ObjectPool`, `FlatMap`, `Forest`, `DataTree`/`DataStream` (serialization), `TaskQueue`, logging. No scene concepts.
- **`vsr_scene`** (`src/vsr/scene/`): `Scene`, `Object`, `Parameter`, `Layer`/`Forest` (instancing), `Animation`, `UpdateDelegate`. Mirrors ANARI's object hierarchy.
- **`vsr_io`** (`src/vsr/io/`): 30+ file format importers (OBJ, GLTF, PLY, USD, VTK, ASSIMP, etc.), volume importers (RAW, NanoVDB, VTI), procedural generators, and VSR scene serialization.
- **`vsr_rendering`** (`src/vsr/rendering/`): `RenderIndex` (VSR-to-ANARI sync), `ImagePipeline` (composable render passes), camera manipulators.
- **`vsr_app`** (`src/vsr/app/`): `ANARIDeviceManager`, `Context` (bundles scene + render index + pipeline), CLI parsing, `renderAnimationSequence`.
- **`anari_vsr`** (`src/anari_vsr/`): ANARI device implementation that mirrors ANARI state into a VSR scene; writes `live_capture.vsr` on each committed frame.

### Key Design Patterns

**Object model**: `Object` holds typed `Parameter` values. Parameters store `vsr::core::Any` (ANARI-typed). Use-count tracking enables garbage collection. `ObjectPool<T>` manages object lifetime with stable handles.

**Scene graph / layering**: `Scene` owns object databases. A `Layer` is a `Forest` of transform/object nodes enabling instancing. Multiple layers with active/inactive control compose the final scene.

**UpdateDelegate flow**: Mutations to `Scene` fire `BaseUpdateDelegate` callbacks. `MultiUpdateDelegate` fans out to `RenderIndex`, network sync, and UI. Subclass `BaseUpdateDelegate` to intercept changes.

**RenderIndex** (`src/vsr/rendering/index/`): Translates VSR scene state to live ANARI handles. `RenderIndexAllLayers` and `RenderIndexFlatRegistry` are the two strategies. Populated via `populate()`, updated incrementally via delegate callbacks.

**ImagePipeline** (`src/vsr/rendering/pipeline/`): one `ImageSource` (e.g. `AnariSceneRenderPass`, `MultiDeviceSceneRenderPass`, `ExternalFrameSource`), then ordered `ImagePass`es (tone map, outlines, AOV visualization, overlays), then `ImageSink`s (SDL texture, network copy-out). See ADR 0028.

**anari_vsr device modes**:

1. *Internal scene* (default): creates its own `Scene`, renders via a backend device (controlled by `ANARI_VSR_LIBRARY`, default `helide`), writes `live_capture.vsr`.
2. *External scene*: caller provides a `vsr::scene::Scene*` via the `"scene"` device parameter; ANARI state is mirrored into the caller's scene.

### Tutorial Apps

`src/apps/tutorial/` contains single-file examples covering specific concepts (render, pipeline, multi-render, forest, DataTree, load/save, USD export). These are the best starting point for understanding how the libraries compose.

### Lua Scripting (`src/vsr/scripting/`, enabled with `VSR_USE_LUA=ON`)

`vsrLua` is a standalone interpreter; `vsrViewer` embeds a Lua terminal. Scripts have a pre-bound `scene` variable. The `scripts/init.lua` populates viewer Actions menus. See `src/vsr/scripting/README.md` for the full API and `scripts/examples/` for worked examples.

Lua module search paths (lowest to highest priority):

1. `<source>/scripts/` (dev builds)
2. `<install>/share/vsr/scripts/`
3. `~/.config/vela/scripts/`
4. `VSR_LUA_PACKAGE_PATHS` (`:` separated)

### User Config Directory

Per-user state lives in `~/.config/vela/` (`$XDG_CONFIG_HOME/vela/` on Linux
when set, `%APPDATA%\vela\` on Windows), resolved only by
`vsr::core::userConfigDirectory()`. It holds `preferences.vsr` (Application
Preferences), `<applicationIdentifier>/uiState.vsr` (each imgui app's UI
State, saved at exit), `colormaps/` and `scripts/`. It is named for the
product, not the library: see
[docs/adr/0041](docs/adr/0041-name-the-user-config-directory-after-vela.md)
and [docs/adr/0040](docs/adr/0040-keep-ui-state-per-application-not-per-document.md).

### Environment Variables

| Variable | Purpose |
|---|---|
| `VSR_ANARI_LIBRARIES` | Comma-separated Device Identifiers (`[subtype@]library`) shown in `vsrViewer` device selector |
| `ANARI_VSR_LIBRARY` | Backend Device Identifier used by `anari_vsr` device (default: `helide`) |
| `VSR_LUA_PACKAGE_PATHS` | Additional Lua module search paths |
