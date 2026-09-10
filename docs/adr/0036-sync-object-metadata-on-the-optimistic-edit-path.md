# Sync object metadata on the optimistic edit path

Object metadata — the free-form keyed values a `vsr::scene::Object` carries
alongside its parameters, like a camera's `manipulator.*` or a volume's
`opacityControlPoints` — is a third state category that ADR 0030 did not
name, and it now travels the same lane as parameter values: optimistic,
one-way, per-key. `Object::setMetadataValue` signals the update delegate the
way a parameter write already does, the Structural Mirror turns that signal
into `SetObjectMetadata`, and metadata writes join the enclosing parameter
batch, so one frame of a camera orbit sends one message rather than six.
Metadata was already a first-class citizen of the disk format
(`serialize_Object`) and of bulk transfer (`TransferScene`, `NewObject`); it
was a non-citizen only of the incremental client-to-server path, and that
asymmetry is what this decision closes.

The gap was invisible because it lived in the object model rather than in the
protocol. `UpdateDelegate` declares seventeen signals and none of them is a
metadata signal, so the mirror was never dropping metadata — it was never
told. The visible symptom was a camera that reconnected to an identical view
and then jumped on the first drag: with no metadata on the incremental path,
the server rebuilt its manipulator with `updateManipulatorFromCameraPose`,
which is lossy by construction because an orbit centre and distance cannot be
recovered from a position/direction/up triple. The invented centre was then
written to the camera rig, persisted, and faithfully restored. Promoting the
six `manipulator.*` keys to real Camera parameters was rejected because
parameters are pushed to ANARI and no device wants them. A bespoke
manipulator message was rejected because it fixes one of the two known keys
and leaves `opacityControlPoints` — written on every transfer-function
opacity drag — silently discarded, which is a second and undocumented blocker
on deferred transfer-function editing beyond the missing `SetArrayData`.
Declaring the status quo intentional (metadata is bulk-only, server to
client) was rejected because ADR 0030's own example set names cameras and
color-map transfer functions, so a reader applying it literally concludes
this state is already covered by the optimistic path.

The camera's manipulator state needs no new home. The camera rig is already
the durable per-shot store: `followCameraEdit` writes `rig->current`,
`applyActiveShot` samples it back and derives the camera object's metadata
from it, `TransferScene` carries that metadata at bootstrap, and the client
adopts it through `updateManipulatorFromCamera`. Every step of that chain
already worked; only the lossy one is replaced. A per-shot Working View field
on `Shot` was considered and rejected as redundant with the rig. Three gaps
in rig-as-home are known and deliberately deferred: a rig with keyframes
never receives `current` — the guard that stops an orbit from clobbering an
authored camera animation — so a keyframed shot's working viewpoint is not
stored anywhere; a shot with no rig assigned has nowhere to store one; and
two shots sharing a rig share a viewpoint. A distinct Working View is the
answer if those are ever worth closing, and this ADR is the record that they
were seen and left alone.
