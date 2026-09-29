# Give Image Pipelines a single source, and pick outside them

An Image Pipeline has exactly one Image Source, followed by Image Passes and
then Image Sinks. Picking is a Pick Request made against a pickable source,
not a pass inside the pipeline. The previous design let any pass act as the
source by being first (`stageId == 0`). A later ANARI pass would then
depth-composite onto it. Picking was a self-disabling `PickPass`, and it forced
a full synchronous pipeline render, including tone mapping, outlines and the
display upload, just to read one pixel.

A reader will see the depth-composite code that used to live in
`AnariSceneRenderPass` and may want to bring back the ability to stack several
sources. Nothing in the tree ever used it. The one real multi-device case
already composites inside `MultiDeviceSceneRenderPass`. Keeping that option
open also forced every pass to guess its role from its position. With a single
source, the source can decide which channels to produce from what the enabled
passes declare they read. With several sources, that demand would need to be
split between them. If compositing sources is ever needed again, build it as a
compositing Image Source that wraps other sources. Do not reintroduce an
ordering convention.

Picking stays outside the pipeline so that it never changes the displayed
image and never pays for passes or sinks. It is a capability of sources that
can produce one synchronous frame with depth and IDs. Multi-device and
remote-frame sources are not pickable, which matches how they behaved before.
A Pick Request returns a decoded hit (depth, object type and index, instance,
primitive, world-space point), so the volume-ID bit and the ray reconstruction
are no longer spread across UI code.

See [`src/vsr/rendering/CONTEXT.md`](../../src/vsr/rendering/CONTEXT.md) for
the vocabulary.
