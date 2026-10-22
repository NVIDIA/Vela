# Share repeated USD objects by content, by default

ADR 0046 shares geometry Arrays between the copies a Stage repeats by plain
reference, but left every copy its own Geometry, Material, Sampler and Surface.
On the Davinci `rt_010` shot (`material=renderLow`) that is 6,901 Geometries
holding 1,703 distinct contents, 3,086 Materials holding 215, and 2,636
Samplers holding 432. Host memory hardly depends on it: the Arrays were already
shared. Device work does. A Surface is what a device's acceleration structure
is built over, so as long as every copy has its own Surface, a device builds
one for every copy.

The USD importer now shares those objects too, by content and within one
Import, and does so **by default**. After building an object it looks for one
the Import already built with the same content and, if it finds one, removes
the new object and uses the existing one. It works bottom up, because
`sameContent()` compares object parameters by identity: Samplers before the
Material that reads them (`shareMaterial()`), and Geometry and Material before
the Surface that pairs them (`buildParts()`). With object sharing on, the shot
above has 1,703 Geometries, 257 Materials, 630 Samplers and 1,755 Surfaces;
peak import memory (21.8 GiB) and import time (about 2:40) are unchanged.

Content means what a device would see: type, subtype and every parameter's
value, enabled state and usage hint. Names and metadata are not content. The
comparison is a general `vsr_scene` helper (`sameContent()` and
`contentHash()`), not USD-specific code, because nothing in it depends on
where the objects came from.

## Considered options

- **Opt-in.** This keeps the old editing semantics, but leaves the device win
  off for exactly the large scenes that need it. A Surface can be shared only
  if its Material is, so making Material sharing opt-in amounts to making
  Surface sharing opt-in.
- **Share Geometry only.** It changes nothing a user is likely to edit, but it
  gains nothing on a device: two Surfaces over one Geometry are still two
  Surfaces.
- **Decide sameness from composition** (two references to one asset). This
  was rejected for the reasons ADR 0046 gives for Arrays.

## Consequences

- Editing a shared object edits every copy. Picking one prop and changing its
  Material's colour changes every copy of that prop. This matches USD:
  changing the referenced asset's material would do the same, and native USD
  Instances already share their objects. An Import that needs every copy
  editable on its own sets `UsdImportOptions::shareRepeatedObjects` to false.
  Arrays are still shared then, as ADR 0046 decided.
- A shared object keeps the name of the first copy that built it: two copies
  of `/Asset/Quad` share one Geometry named `/CopyA/Quad`. Each placement
  still has its own layer nodes, named after its own prims, which is where
  per-copy identity lives.
- A deforming gprim's Geometry and Surface are never shared, for the same
  reason its Arrays are not: its binding re-fills that Geometry every frame
  (ADR 0022). Its Material can still be shared.
- Copies whose images are equal but held in separate Image Cache entries do
  not share Samplers, because the Sampler compares its image Array by
  identity. On the shot that leaves 630 Samplers for 432 distinct contents.
- The Surfaces a device sees are now shared, but RenderIndex still builds one
  ANARI Group per placement, so devices still build acceleration structures
  per copy. ADR 0048 reuses Groups so that sharing reaches the device.
