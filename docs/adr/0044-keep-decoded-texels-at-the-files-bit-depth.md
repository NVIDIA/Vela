# Keep decoded texels at the file's bit depth

The shared decode path in `io/images/detail/decoders.cpp`, which OBJ, USD, PBRT,
and ASSIMP textures all go through, used to expand every image to
`ANARI_FLOAT32_*` via `stbi_loadf`, taking sRGB to linear in software with a
`pow(x, 2.2)` curve. No record says why; it was inherited from TSD's first
texture importer, and `isLinear` was later bolted on by toggling stb's gamma.
It quadruples an 8-bit texture: on the Davinci USD scene's `Interior` assembly,
130 textures held 17.8 GiB as float and 5.9 GiB at their own depth, and peak
import memory fell from 23 to 11 GiB. Texture *sharing* was never the problem:
the Image Cache already decoded each file once.

Texels now keep the file's bit depth. 8-bit files stay 8-bit, with color ones
in an `ANARI_UFIXED8_*_SRGB` format so the device applies the true sRGB EOTF on
fetch -- the contract `import_GLTF` always used, so the tree now has one rule
instead of two. 16-bit files stay 16-bit; ANARI has no 16-bit sRGB format, so
color ones are linearized in software and stored as `ANARI_UFIXED16_*`. Only
float files (Radiance HDR, EXR, float TIFF) decode to float. Alpha stays linear
throughout.

## Consequences

- 8-bit sRGB textures through the shared path now use the exact sRGB curve
  rather than gamma 2.2, a slight midtone shift that matches glTF.
- Devices must accept `UFIXED8`, `UFIXED8_*_SRGB`, and `UFIXED16` image arrays.
  Code that inspects a sampler's image must not assume a float element type
  (`import_PBRT`'s grey-broadcast check did).
- A new importer that decodes its own texels should hand them to
  `ImageCache::acquireDecoded` at their native type rather than expanding them.
