# Key Stage Sessions by file and Variant Selections

Production USD assets often expose their own levels of detail as variant sets:
every prop in the Davinci scene has a `material` set offering `render` (4K
textures, about 160 GiB of texels across the scene), `renderLow` (1K, 8.5 GiB)
and `renderXLow` (256², 0.5 GiB), each pulled in by a payload. An Import Option,
`variantSelections`, now chooses among them: a map from Variant Set name to
variant, applied to every prim that has the set and authors the variant.

The selections are authored in the Stage's session layer, which outranks every
opinion in the files and is never written back to them, and they are made
*before* any payload loads: a selection Stage opens with `LoadNone`, selects,
then loads, alternating until a pass neither selects nor loads anything new,
since a loaded payload can author Variant Sets of its own. A deselected
variant's payload is therefore never composed, which is the whole point:
selecting after a full load would read the high-detail content and then
discard it. (A chosen variant can still pull in another variant's layers
itself -- Davinci's `renderLow` sublayers the `render` shader layer and
overrides only its texture paths -- and those layers open as the asset
intends; the 4K textures they name are never decoded.)

On the full Davinci `rt_010` shot, selecting `material=renderLow` takes the
import from not fitting in 90 GB to completing at an 84 GiB peak, with texture
memory at 1.4 GiB; what remains is geometry.

Because the selections change what the Stage composes to, they are part of a
Stage Session's identity. ADR 0021 keyed Sessions by file alone and kept
every Import Option out of the Session; Variant Selections are the one
exception. The registry key is the canonical path plus the ordered selections,
and every USD animation binding stores its selections beside its file path, so
an Archive rejoins the same Stage. Keying by file alone would let an import
with no selections silently join a held-open Session that has them, and read
the wrong content.

## Considered Options

- **Select inside the resolution chain** (a filtering scene index): rejected,
  because by then the payloads are already loaded; it saves nothing.
- **Mask the Stage** (`UsdStagePopulationMask`): masks prune by path, not by
  variant, so they cannot express "whichever variant is called `renderLow`".
- **Downsample textures on import**: still worth having for Stages that offer
  no variants, but it reads every full-resolution file first and discards the
  asset's own, better-filtered lower levels of detail.

## Consequences

- Prims inside a native USD Instance are not selected individually: an
  instance proxy cannot carry an opinion. A Variant Set on the instanceable
  prim itself is selected like any other.
- A selection naming a variant a prim's set does not author leaves that prim's
  own selection in place and is listed in the Import Report; a set no prim has
  is logged.
- Two imports of one file with different selections hold two Stages open.
