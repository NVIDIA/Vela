# Separate Color Maps from Transfer Functions

A **Color Map** is a project-owned, named asset: a `ColorMapRecord` of an id
and a name, paired by naming convention with a loose `<id>_colormap` Array.
A **Transfer Function** is a `Volume`'s own value-to-color-and-opacity
mapping: its `"color"` parameter Array, its `opacityControlPoints` metadata
array, and the `valueRange`, `opacity` and `unitDistance` parameters beside
them. Nothing binds one to the other, and this decision records that the
separation is intended rather than unfinished.

The SciVis Studio glossary previously defined only the Color Map, listed
"transfer function" under _Avoid_ as a name for the editing widget, and said
the mapping's values "live on a scene-side object the project entry refers
to" -- a binding that no code has ever built. `TransferFunctionEditor` does
not read or write `<id>_colormap` Arrays; it overwrites the selected Volume's
own `"color"` Array. The remote README inherited the glossary's claim and
framed the deferred client-side editing feature entirely around
`ColorMapRecord`, so its two named blockers describe an object the feature
does not touch, and its report that opened projects lose their samples is
true only of those unbound Arrays. Volumes' transfer functions persist
correctly, inside each dataset's archive.

Binding volumes to Color Maps was rejected for now. It is a coherent feature
-- a named ramp reused across volumes, edited once and moving all of them --
but it is a larger one than making the existing editor work remotely, and it
brings its own questions about sharing semantics and about what an edit
through one volume means for the others. Removing the unbound
`<id>_colormap` machinery was also rejected: `CreateColorMap`,
`RenameColorMap` and `RemoveColorMap` are already on the wire with scenarios
behind them, so deleting the Arrays would mean changing the protocol to
withdraw a feature while adding another. They stay, documented as the
placeholder they are.

So "transfer function" is promoted from a banned word to a defined term. The
ban made sense while only one concept was modelled; with two, a glossary that
forbids the word used by the core type (`vsr::core::TransferFunction`), the
widget, and ANARI's own `transferFunction1D` subtype misleads every reader it
reaches. The risk this leaves is the opposite one -- two similar terms that
must be kept apart -- and the glossary entries carry that weight now.
