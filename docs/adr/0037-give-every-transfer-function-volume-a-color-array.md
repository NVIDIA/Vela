# Give every transfer-function Volume a color Array

A `Volume` of subtype `transferFunction1D` reaches its consumers with a
256-entry `ANARI_FLOAT32_VEC4` color Array already bound. The scalar `float3`
form of the `"color"` parameter is no longer a state a volume rests in:
`Volume::ensureColorArray` binds one if the parameter does not already hold
an Array, every volume-creating importer and procedural generator calls it
(most already bound an Array of their own and are unaffected), and a dataset
archive written before this decision is migrated when it loads.
`TransferFunctionEditor`'s promote-on-first-named-map branch becomes
unreachable and the editor only ever overwrites samples in place.

The invariant is a post-condition of creation rather than a constructor side
effect, and deliberately does not run during deserialization. Creating an
Array mid-restore would take the next object index and shift every index
after it, which the archive's own index-mismatch check would reject and which
would break the `LayerNodeNumbering::Preserved` parity the thin client's
mirror depends on. A restored volume brings its own Array anyway; only an
archive predating this decision does not, and that case is handled once the
subtree is fully loaded and its indices are all assigned.

The reason is the thin client. Promotion is `createArray` followed by
`setParameterObject("color", ...)` -- an object creation and an array binding.
Both are structural, both are refused client-side by `mirrorEditPolicy`
because no client-to-server message expresses them, and object identity is
minted on the server (ADR 0029), so the client cannot invent the Array even
if the policy allowed it. A volume whose color is still scalar is therefore
not editable from the client at all, and the first knob drag on a
freshly-imported dataset is exactly when a user meets one.

A round-tripped `PromoteVolumeColor` op was rejected. It is the orthodox
answer under ADR 0030 -- structure round-trips -- but it puts a request, a
reply and a whole-project snapshot in front of the first drag, to establish
state that has exactly one sensible value and cannot fail. ADR 0030 chose
optimism specifically to keep round-trips off transfer-function interaction;
adding one back to set up that interaction honours the letter and loses the
point. Leaving the client to show scalar-color volumes as read-only was
rejected because "this volume is not editable and the fix is to edit it
somewhere else" is not a state a user can act on.

The cost is real and deliberate: this changes shared SciVis Studio behaviour
to serve a client-server constraint. Volumes that would have carried three
floats now carry a kilobyte of samples, and code reading `"color"` can no
longer encounter the scalar form. The alternative was to keep a
representation alive in the monolith solely so that the client could refuse
to edit it.
