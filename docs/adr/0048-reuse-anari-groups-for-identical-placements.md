# Reuse one ANARI Group for placements of the same objects

`RenderIndexAllLayers` turns each transform node that directly holds Surfaces,
Volumes or Lights into an ANARI Group and an Instance. Every node used to get
a Group of its own, even when two nodes held the very same objects. Devices
build their acceleration structures per Group: helide makes one Embree scene
per Group, and VisRTX runs one `optixAccelBuild` per Group. So a placement
repeated a thousand times was built a thousand times, even after ADR 0047 made
its copies share one Surface.

`RenderToAnariObjectsVisitor` now keys the Groups it makes by the exact
Surface, Volume and Light handles they hold, in order, and makes one Group per
key. Each placement still gets its own Instance, so transforms, per-instance
parameters and instance ids are unchanged. On the Davinci `rt_010` shot
(`material=renderLow`, with ADR 0047's sharing), the 7,503 Groups become
1,739, and the triangles that acceleration structures are built over fall
from 1.07 billion to the 201 million that are distinct.

## Considered options

- **Key Groups by object content rather than handle.** Two Surfaces with
  equal content are already one Surface when the importer shares them, so the
  extra comparison would only help scenes whose objects were not shared. It
  would also tie Group reuse to object edits made after the Group was built.
- **Share Groups across layers.** That needs reference counting of Groups
  over every layer's Instances. Reuse is per traversal instead: a Group lives
  as long as the Instances of the one layer sync that made it, so no lifetime
  bookkeeping is added. One Import lands in one layer, which is where repeats
  come from.

## Consequences

- Reuse depends on placements holding the same objects. Content that is
  repeated but not shared (`shareRepeatedObjects = false`, or another
  importer) still gets a Group per placement.
- Handles stay valid for as long as a Group is reused: an edit to a Surface's
  parameters changes the Surface, not its handle, and a structural change
  re-syncs the layer, which rebuilds its Groups.
