# Import miniScene as hayStack renders it

`import_MINI()` reads Ingo Wald's [miniScene](https://github.com/ingowald/miniScene)
`.mini` files into a Scene. hayStack is the renderer `.mini` files are made
for, and ADR [0042](0042-export-miniscene-in-the-form-haystack-consumes.md)
already writes them in the form hayStack reads. So wherever miniScene leaves
the meaning of a field open, the importer reads it the way hayStack does. A
`.mini` file should look the same in VSR as in hayStack, and a scene exported
under 0042 and imported back should render as it did before export.

Each `mini::Object` becomes one set of Surfaces that all of its Instances
share. An object placed once goes under a transform node. An object placed
more than once goes under a single transform array node, which keeps the
layer tree small for scenes instanced millions of times. Lights go in world
space at the import's root node.

Decisions that follow:

- **Texture rows and texcoords pass through unchanged.** `.mini` files do not
  agree on a convention. Vela writes rows top-first with `v` running down
  (0042), and obj2mini writes rows bottom-first with `v` running up. Either
  way the rows and texcoords agree with each other, and hayStack samples both
  as given. The importer gives the Image Cache the texels declared as
  top-down, so neither is flipped and the texture renders as it does in
  hayStack. This does not break ADR
  [0014](0014-store-images-in-anari-orientation.md)'s rule that importers
  never flip texels themselves: the declaration only says the texels are
  already in the order the sampler gets them.
- **8-bit textures are linear.** miniScene has no sRGB flag, and hayStack
  uploads RGBA8 texels as linear, so the importer does the same.
- **Samplers wrap with `repeat`.** hayStack uses `mirrorRepeat` for every
  `.mini` texture. That changes nothing inside [0, 1], but it mirrors tiled
  textures from OBJ sources, which are meant to repeat, and it would turn
  VSR's own `repeat` into something else on a round trip.
- **Env maps are flipped to bottom row first.** `.mini` env maps are top row
  first, and hayStack flips them on load. An `hdri` light wants its radiance
  bottom row first, so the importer flips them and builds the radiance array
  outside the Image Cache, as `import_HDRI` does. The frame is read the way
  hayStack reads it: up = `vz` and direction = `-vx`.
- **Directional irradiance is `2 * average(radiance)`**, which is hayStack's
  formula and the inverse of 0042's halving. The radiance is split into a
  color whose average is 1 and a scalar irradiance, so the light's strength is
  one parameter to edit. Quad lights are split the same way, into `color` and
  `radiance`. hayStack ignores quad lights, so they follow ANARI: the edges are
  ordered so that `cross(edge1, edge2)` points along the light's stored normal.
- **Materials follow hayStack's mapping.** `ANARIMaterial` maps to
  `physicallyBased` field by field, and its integer `alphaMode` is written back
  as ANARI's string. `Matte` maps to `matte` with the reflectance used as the
  color. `Disney`, `Metal`, `MetallicPaint`, `Plastic`, and `Dielectric` map
  to `physicallyBased` using hayStack's formulas, including `alphaMode` =
  `blend`. Two departures add data that hayStack drops but ANARI can hold:
  Disney `emission` becomes `emissive`, and `BlenderMaterial`, which hayStack
  renders gray, maps its principled parameters onto `physicallyBased`.
  `ThinGlass`, `Velvet`, and unknown kinds become the gray matte hayStack uses
  for them.
- **Matte reflectance is not divided by π.** hayStack's `Matte` path sets the
  ANARI color to `reflectance / 3.14`. 0042 writes the VSR color as the
  reflectance, so dividing here would darken every round trip by π. Until
  exporter and importer change together, the two stay inverses of each other,
  not of hayStack.

Content that does not convert (embedded ptex textures, Disney alpha textures,
non-float4 env maps, meshes whose indices are out of range) is skipped, with
one warning per kind. The importer needs `VSR_USE_MINISCENE` and the same
`vela_ext_miniscene` library as the exporter.
