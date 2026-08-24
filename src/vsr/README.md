# VSR Library Collection

This directory contains the main VSR libraries used by applications, tools, and
ANARI integration layers.

## Library Index

| Library | Directory | Description | Build Control |
| --- | --- | --- | --- |
| `anari_vsr` (`vsr_device`, `anari_library_vsr`) | [../anari_vsr/](../anari_vsr/) | ANARI device implementation backed by VSR, useful for capturing ANARI state in a Scene Archive for offline inspection in `vsrViewer`. | Built from `src/CMakeLists.txt` |
| `vsr_algorithms` | [algorithms/](algorithms/) | Image-processing kernels (tone mapping, auto exposure, AOV visualization, outline, buffer ops) with CPU and optional CUDA backends. | Always enabled (CUDA backend requires `VSR_USE_CUDA=ON`) |
| `vsr_app` | [app/](app/) | Application-facing glue for command-line import setup, ANARI device management, selection utilities, and offline sequence rendering. | Always enabled |
| `vsr_core` | [core/](core/) | Foundational utilities including typed value storage, tokens, containers, serialization trees, logging, timing, and task queue support. | Always enabled |
| `vsr_io` | [io/](io/) | Foreign-format importers/exporters, native VSR Archives, component serialization, and procedural scene generators. | Always enabled (some importers depend on optional build flags) |
| `vsr_scene` | [scene/](scene/) | Core scene representation with ANARI-like object hierarchy, parameters, layers, and animation support. | Always enabled |
| `vsr_rendering` | [rendering/](rendering/) | Render index and render pipeline layers that synchronize VSR scenes to ANARI worlds and render passes. | Always enabled |
| `vsr_mpi` | [mpi/](mpi/) | MPI helper types for rank-replicated state used in distributed workflows. | `VSR_USE_MPI=ON` |
| `vsr_network` | [network/](network/) | Boost.Asio messaging and scene synchronization primitives for networked client/server workflows. | `VSR_USE_NETWORKING=ON` |
| `vsr_ui_imgui` | [ui/](ui/) | ImGui-based UI framework and reusable viewer/editor windows and dialogs. | `VSR_BUILD_UI_LIBRARY=ON` |
| `vsr_lua` | [scripting/](scripting/) | Lua bindings and scripting runtime for scene authoring, automation, and viewer action extensions. | `VSR_USE_LUA=ON` |

## Notes

- Each library directory has a dedicated `README.md` with more detail.
- Application examples that consume these libraries are under [../apps/](../apps/).
