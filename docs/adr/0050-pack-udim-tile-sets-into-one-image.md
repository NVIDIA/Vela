# Pack UDIM tile sets into one image

ADR 0019 reports a UDIM tile set as unsupported because an ANARI sampler holds
a single image. It rejected binding tile 1001 alone, since that renders every
mesh spanning more than one tile silently wrong, and it rejected rewriting
MaterialX networks into per-tile branches. It did not consider packing the
tiles into one image.

On the Davinci `rt_010` shot (`material=renderLow`), 940 of 1,727 OmniPBR
materials name a UDIM set for their diffuse texture, and most also do for
roughness and normal. Without the tiles, the OmniPBR mapping falls back to
`diffuse_color_constant`, and for much of the set the artists left that as a
saturated placeholder: blue `(0.12, 0.16, 0.66)` on 105 materials, plus greens
and magentas. That is why the wall paneling, doors, tables, shelves and
fireplace render as flat bright colors. Only 24 of the 668 UDIM sets have a
single tile, so the shortcut ADR 0019 rejected still does not help.

When an Image Cache source names a `<UDIM>` path, the cache now decodes every
tile beside it and packs them into one image on a grid: tile
`1001 + u + 10 * v` goes in column `u`, and in row `v` counting up from the
bottom. Each cell is the size of the largest tile. A smaller tile is
nearest-sampled up to that size, and a tile with fewer channels is widened the
way stb reads channel counts: grey fills R, G and B, and a missing alpha is
opaque. A cell with no tile stays transparent black. `makeImageSampler` then
composes the mapping from the set's uv range onto the grid into the sampler's
`inTransform`/`inOffset`. Importers have already reversed `v` by then (ADR
0014), so tile row `t` covers `v` in `[-t, 1 - t]`, and the mapping is affine:
`u / columns` and `(v + rows - 1) / rows`. Any device that implements
`image2D` can then read the set; nothing device-specific is needed.

This applies to the portable `physicallyBased` path, through `importTexture`:
the OmniPBR mapping and the UsdPreviewSurface reader. MaterialX documents
still carry the `<UDIM>` path to the device unchanged and are still reported
as ADR 0019 describes, because a document's image node samples raw texture
coordinates and has no sampler transform to compose the grid into.

## Consequences

- Bilinear filtering at a tile's edge blends in the neighbouring cell, so a
  thin seam can show where a mesh crosses from one tile to the next. Padding
  each cell with a border of its own texels would remove it; that is left
  until a seam is actually seen.
- Empty cells and up-sampled tiles cost memory the files did not. On Davinci
  the grids have 3,859 cells for 3,693 tiles, and only ten sets (the books)
  mix tile sizes or channel counts.
- A set whose tiles differ in component type (8-bit beside 16-bit, say), or
  that holds a block-compressed tile, does not decode, and the binding is
  reported as a failed texture load as before.
- `Image` carries the grid it was packed on, and content sharing (ADR 0049)
  compares it, because the same texels on a different grid need a different
  sampler transform.
- A device with native tiled-texture support could take the tiles as they
  are. If ANARI or a vendor extension gains a tiled sampler, Vela should emit
  it on devices that report it and keep this packing as the portable fallback.
