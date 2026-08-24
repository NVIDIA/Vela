## IO Library (`vsr_io`)

`vsr_io` handles foreign-format conversion, native VSR Archives, procedural
scene generation, and component serialization.

### High-Level Concepts

- File importers for full scenes and geometry datasets:
  AGX, ASSIMP, AXYZ, DLAF, E57, ENSIGHT, GLTF, HDRI, HSMESH, NBODY, OBJ, PDB,
  PLY, POINTSBIN, PT, SILO, SMESH, SWC, TRK, USD, VTP, VTU, XYZDP.
- Volume/spatial-field importers (`import_RAW`, `import_NVDB`, `import_VTI`,
  etc.) and `import_volume()` dispatch helpers.
- Procedural generators for test and demo scenes (`generate_randomSpheres`,
  `generate_icosphere`, `generate_default_lights`, and others).
- Native Scene, Object, Layer Subtree, Camera, Renderer, Animation, and
  Animation Manager Archives, exposed through `archives.hpp`.
- Reusable component serialization between VSR objects and
  `vsr::core::DataTree` nodes, exposed through `serialization.hpp`.
- Foreign export helpers for scene-to-USD and structured-volume-to-NanoVDB,
  exposed through `exporters.hpp`.

### Why Use This Library

- You want a single API surface for importing many scene and volume formats.
- You need deterministic generated content for tests, demos, or device bringup.
- You want to save/load native VSR Archives or export scene data to other
  tools.

### Build Notes

- Optional importer backends are controlled by CMake options such as
  `VSR_USE_ASSIMP`, `VSR_USE_HDF5`, `VSR_USE_USD`, `VSR_USE_VTK`,
  `VSR_USE_SILO`, and `VSR_USE_TORCH`.
