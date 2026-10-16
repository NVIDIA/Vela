## App Library (`vsr_app`)

`vsr_app` provides reusable application-level glue on top of `vsr_scene`,
`vsr_io`, and `vsr_rendering`. It owns Application Dumps; VSR I/O owns the
Scene and Animation Manager Archives composed into them (see
[ADR 0011](../../../docs/adr/0011-distinguish-archives-imports-and-dumps.md)).

### High-Level Concepts

- `Context` bundles application options, Scene, Animation Manager, ANARI device
  state, camera state and offline settings.
- `ANARIDeviceManager` discovers and loads ANARI libraries, tracks live
  devices/extensions, and manages per-device RenderIndexes.
- Generic applications use `Context::parseCommandLine()` and
  `Context::setupSceneFromCommandLine()` for importer routing. The headless
  renderer prepares inputs explicitly instead of inheriting their convenience
  scene, Camera, lighting or fallback policies.
- `OfflineRenderJob` and `renderOfflineJob()` render resolved stills or selected
  timeline frames on a fresh isolated device. `renderAnimationSequence()` adapts
  the existing interactive sequence workflow to this same engine.
- Selection and saved camera-pose helpers support viewer/editor workflows.
  Offline rendering neither selects nor converts saved poses; their storage
  and unrelated interactive uses remain unchanged.

### CLI and UI adapters

`vsrRender` is the general headless rendering executable. Its
[canonical CLI reference](../../apps/tools/README.md#vsrrender) covers all
options, defaults/saved/explicit precedence, input/import routing, Camera
selection, renderer-value grammar, lighting, AOVs, outputs and optional MPI.
`vsrRender --help` provides direct discovery and examples.

```sh
vsrRender scene.vsr --camera 'Main Camera' --width 320 --height 240 \
  --samples 4 --output preview.png
```

The CLI accepts one native Scene Archive or Application Dump in either Binary
or Text Encoding, plus ordered additive foreign imports. Native contents, not
extensions, determine meaning; failed dump reconstruction never falls back to
scene loading. Native loading precedes additive imports. Consumed settings and
required Archives are validated, while inert saved pose internals do not select
or block an otherwise valid scene Camera. No schema or encoding changes are
introduced by rendering.

The adapter resolves a still by default, requires a scene Camera or explicit
`--default-camera`, and adds no implicit lights or demo content. There are no
stdin prompts, pose batches, automatic sequences or device fallback. A generated
Camera fits once at the global first selected frame; later animated motion may
leave its fixed view. For executable examples, see the reference's
[Camera controls](../../apps/tools/README.md#camera-controls),
[quality/AOVs](../../apps/tools/README.md#quality-and-aov-visualization),
[frame/output policy](../../apps/tools/README.md#frame-selection-and-sequence-output)
and [MPI](../../apps/tools/README.md#optional-mpi-frame-distribution) sections.
These policies belong to the adapter, not a second rendering implementation.

The interactive `renderAnimationSequence()` adapter retains configured ranges
and stride, four-digit prefix filenames, overwrite behavior, Camera-index-zero
fallback and legacy callback totals. Nonanimated UI jobs keep their configured
number of repeated frames rather than the CLI's still default. Configuration
errors return `Failed`; unexpected exceptions propagate just as in the resolved
API. The adapter remains necessary for `OfflineRenderModal` and is not a second
engine.

`OfflineRenderModal` invokes the adapter on the application task queue. Its
atomic cancellation flag is checked before each frame. Completed/cancelled jobs
close as before; failures remain visible with diagnostic, destination and
completed-frame count. The worker catches standard/nonstandard exceptions at
the application boundary and reports interruption without inventing an engine
result or completed-frame count. Destruction waits for the worker, cancellation
is cooperative, and completed failed tasks can be retried. The isolated offline
device and temporary RenderIndex never replace viewport resources.

Offline lens settings live on the selected Scene Camera, not in a second
offline configuration. The settings UI edits its existing lens parameters; new
Application Dumps save them only in the Scene Archive. Legacy duplicate offline
aperture/focus fields are ignored on load (the CLI warns), even for nondefault
values. Explicit CLI lens flags remain validated temporary overrides.

### Resolved offline rendering

The public contract is declared in
[`renderAnimationSequence.h`](renderAnimationSequence.h).
`renderOfflineJob(Context &, const OfflineRenderJob &, callback)` accepts:

- A scene Camera index, or explicit generated-Camera intent with an invalid
  `cameraIndex` and a valid `globalFirstSelectedFrame`.
- A typed renderer Object, Device Identifier (`[subtype@]library`), positive
  dimensions/sample count and existing AOV visualization settings.
- An ordered list of timeline indices with exact PNG destinations.
- Ordered typed overrides, selected-Camera parameters and optional look-at
  target. Assignments apply after each animation evaluation; duplicates are
  last-wins. Device metadata and Camera geometry validate supported controls.

The engine does not parse arguments, load inputs, choose a Camera, fall back to
another device, name outputs, create directories, decide overwrite permission,
or schedule MPI ranks. Adapters resolve those obligations before calling it.
In particular, callers passing exact paths get the shared writer's existing
replacement behavior, not CLI preflight protection. MPI adapters partition only
`frames` and retain `globalFirstSelectedFrame` unchanged, so Camera fitting never
uses a rank-local first frame. App and the UI have no MPI dependency.

#### Synchronous borrowing and reusable jobs

Keep the Context, its Scene, renderer and referenced override targets alive until
`renderOfflineJob()` returns or throws, including during callbacks. The renderer
is borrowed, not copied or owned. Construct `OfflineParameterOverride` only from
live Scene objects: it captures Scene/type/index identity rather than retaining
a raw object address. Engine-created Camera pool growth therefore cannot
invalidate the running request or a subsequent reuse of the caller's job.
Use `object()` or Scene refs to obtain fresh addresses after allocation.

While retaining a job, do not delete/replace its target objects, defragment their
Scene slots, or destroy the Scene. Callbacks must not invalidate any borrowed
object, including the renderer. Stable identity protects against engine-induced
relocation, not arbitrary caller mutation/deletion or asynchronous ownership.

#### Results, restoration and exceptions

`OfflineRenderResult` distinguishes `Completed`, `Cancelled` and `Failed`, with
completed-frame count, current/last frame, destination and diagnostic. The
pre-frame callback receives the actual timeline index, list position and total;
returning false cancels before writing that frame. Expected invalid setup,
device/render/buffer and PNG-write failures return `Failed`. Unexpected
exceptions, including throwing callbacks/observers, propagate to the calling
application. The CLI reports them at its phase boundary (collectively for MPI);
the UI worker reports them at its task boundary.

Validated authored override targets are snapshotted **before any job-driven
animation evaluation**, including generated-Camera preparation. Snapshots are
deduplicated by target and parameter name. On every exit the engine restores:

- The exact Animation Manager runtime clock, including fractional time,
  playback, loop state and pending tick time. Non-overridden animated values
  are evaluated at the restored clock.
- Exact authored override entry values/types, metadata, enabled state and
  presence/absence, even when entry edits differ from evaluated animation.

Authored Camera subtype and unspecified lens/projection settings are not replaced
by generic perspective defaults. Non-overridden Camera animation follows the
restored clock like other animation. Output aspect is supplied to the isolated
ANARI Camera, not persistently authored on the Scene Camera.

Temporary generated Cameras and delegate registrations are removed, and all
rendering resources are released on success, cancellation, expected failure and
unexpected exception. Ordinary restoration attempts independent steps before
propagating its first notification exception. During unwinding, a nonthrowing
fallback suppresses secondary cleanup exceptions to preserve the primary one.
Arbitrary observer side effects are not rolled back; an observer that prevents
its own evaluation/removal notification from finishing must be repaired before
retry. Restoring state is not a promise to undo arbitrary callback edits.

### Rendering composition

One temporary RenderIndex synchronizes Scene animation updates into ANARI. Each
selected image uses a fresh ANARI Frame for isolated sample accumulation. The
engine composes the existing single Image Source, ordered Image Passes and Image
Sinks with demand-derived channels; it does not introduce another renderer or a
save pass. Requested mapped channels are validated before visualization, and
`saveImage()` writes the completed PNG **outside** the Image Pipeline. PNG sRGB
color and vertical orientation are preserved; scene texture row-order ownership
remains the separate [ADR 0014](../../../docs/adr/0014-store-images-in-anari-orientation.md)
contract. See [VSR Rendering](../rendering/README.md) for pipeline composition.
