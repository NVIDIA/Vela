## Rendering Library (`vsr_rendering`)

`vsr_rendering` bridges `vsr_scene` data to live ANARI worlds and defines a
composable render-pass pipeline.

### High-Level Concepts

- `RenderIndex` keeps ANARI objects synchronized with scene changes via the
  scene update-delegate API.
- Two render-index strategies:
  `RenderIndexAllLayers` (layer-aware instancing) and
  `RenderIndexFlatRegistry` (flat object registry).
- `ImagePipeline` chains render passes over shared `ImageBuffers`.
- Passes include ANARI scene rendering, multi-device rendering/compositing,
  picking, outline visualization, AOV visualization, image copy, and file save.
- View/camera tools:
  `Manipulator`, camera-path sampling utilities, and helpers to map manipulator
  state to ANARI camera objects or VSR camera objects.

### Why Use This Library

- You want incremental scene-to-ANARI synchronization without re-uploading
  everything each frame.
- You need to assemble custom rendering workflows from reusable passes.
- You want consistent camera manipulation logic across interactive and offline
  tools.

### Build Notes

- CUDA, TBB, and SDL3 integration are selected by CMake options
  (`VSR_USE_CUDA`, `VSR_USE_TBB`, `VSR_USE_SDL3`).
