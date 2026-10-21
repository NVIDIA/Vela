# Share geometry Arrays by content within an import

A USD Stage often repeats an asset by plain reference rather than marking it
instanceable: the Davinci `rt_010` shot places its props that way, with no
native USD Instances at all. Each placement is a different prim path, so the
importer converted each one from scratch, and 6,901 Geometries held 76 GiB of
Arrays whose distinct content was 14 GiB.

The USD importer now shares geometry Arrays by content within one Import. When
`buildParts()` is about to create an Array, it first asks the Import Context's
`ArrayContentCache` for one already built with the same element type, count
and bytes -- found by hash, confirmed by `memcmp` -- and binds that instead.
Each placement still gets its own Geometry and Surface, so materials and
per-geometry parameters stay per placement; only the buffers are shared, which
the Scene and render index already support (a mesh's subset Surfaces share one
position Array, ADR 0020), and which shares the device's ANARI Array too. On
the shot above, peak import memory falls from 84 to 22 GiB for about 9 seconds
of hashing in a 2.5-minute import.

Content is the key, rather than composition, because content is what actually
repeats. Deciding from the composed prim that two references "are the same
asset" means reasoning about every opinion that could override one copy --
fragile, and blind to repeats that do not come from references at all.

## Consequences

- A deforming gprim's Arrays are never shared, in either direction: its
  binding re-fills them in place every frame (ADR 0022), and sharing one would
  move every prim whose geometry matched its first frame. `geometryDeforms()`
  is the single test both the sharing and the binding use.
- Editing one placement's vertex Array in place now edits every placement that
  shares it, as it already did for instanced USD content. Replacing the Array
  on one Geometry affects only that Geometry.
- Sharing is per Import, not per Scene: two imports of the same Stage still
  hold two copies.
- Geometry and Material objects are still one per placement. Sharing those too
  would cut object counts (and could let devices build one acceleration
  structure per unique mesh), but saves little host memory once the Arrays are
  shared.
