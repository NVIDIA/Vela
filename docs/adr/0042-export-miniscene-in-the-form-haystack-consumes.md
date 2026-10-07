# Export miniScene in the form hayStack consumes

`export_SceneToMiniScene()` writes a Scene to Ingo Wald's
[miniScene](https://github.com/ingowald/miniScene) `.mini` format. In practice
hayStack's ANARI renderer is the only consumer of that format. So whenever
miniScene leaves something open, the exporter writes what hayStack reads back
correctly, rather than what the format could hold in principle. Content
miniScene cannot represent (volumes, cameras, non-triangle geometry, point,
spot and ring lights, vertex colors) is skipped, with one warning per kind.

The export walks what renders: active Layers and enabled nodes. Objects are
grouped the way the render index groups them, so surfaces placed by one
transform node become one `mini::Object`, and each placement of that node
becomes a `mini::Instance`. A transform array becomes one Instance per
matrix.

Decisions that follow from targeting hayStack:

- **Texture rows are flipped.** ANARI images are stored top row first
  (ADR [0014](0014-store-images-in-anari-orientation.md)). The `.mini` files
  produced by miniScene's own OBJ importer store the bottom row first and keep
  uv unchanged, so the exporter flips image rows and leaves texcoords alone.
  **Environment maps are not flipped.** miniScene's `addEnvLight` tool writes
  them top row first, and hayStack flips them on load to compensate.
- **8-bit textures are copied byte for byte, sRGB included.** miniScene has no
  sRGB flag and hayStack uploads RGBA8 as linear, just as it does for every
  `.mini` file made by obj2mini. Decoding sRGB to float would quadruple texture
  size, and it would make exported scenes disagree with every other `.mini`
  file.
- **Directional radiance is halved.** hayStack sets an ANARI directional
  light's irradiance to `2 * average(radiance)`. Writing radiance as
  `color * irradiance / 2` makes the light arrive in hayStack with the
  irradiance it had in VSR.
- **The env-map frame uses hayStack's reading.** hayStack interprets an
  `EnvMapLight` transform as up = `vz` and direction = `-vx`. That is the
  opposite sign of `vx` to the one `addEnvLight` writes.
- **`physicallyBased` maps to `mini::ANARIMaterial`.** `matte` maps to
  `mini::Matte`, except that a matte material with a texture or with opacity
  below 1 becomes the equivalent non-metallic, fully rough `ANARIMaterial`,
  because `Matte` has neither.

The dependency is opt-in (`VSR_USE_MINISCENE`, OFF by default). miniScene has
no releases, so a commit is pinned through `anari_sdk_fetch_project`. Vela
compiles only the core `Scene.cpp` and `Serialized.cpp` into
`vela_ext_miniscene` instead of using miniScene's CMake. That CMake always
builds the importers and tools, can write `CMAKE_BUILD_TYPE` and the output
directories, and defines its own `stb_image` target. At the pinned commit,
`SerializedScene` never collects `ANARIMaterial` textures, so `Scene::save()`
drops all of them. The build compiles a copy of `Serialized.cpp` patched at
configure time, and configure fails if a pin bump moves the patched code.
Drop the patch once the fix is upstream. miniScene ships no LICENSE file;
its sources carry Apache-2.0 headers.
