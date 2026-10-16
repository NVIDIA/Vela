## Command-Line Tools

This directory contains utility executables for data conversion, scene inspection,
headless rendering, and scripting.

## Tools Overview

| Tool | Purpose | Usage |
| --- | --- | --- |
| `obj2header` | Convert a Wavefront OBJ mesh into a generated C/C++ header with packed vertex arrays. | `./obj2header <file.obj> <outfile.h>` |
| `vsrPrint` | Print a `.vsr` file as the Data Tree Text Encoding. | `./vsrPrint <file.vsr>` |
| `vsrConvert` | Convert a `.vsr` file between the Binary and Text Encodings. | `./vsrConvert [--text\|--binary] <input.vsr> <output.vsr>` |
| `vsrAssemble` | Load scene inputs with the same importer flags as the viewers and save them as a Scene Archive. | `./vsrAssemble [-o <file.vsr>] <scene inputs...>` |
| `vsrRender` | Render noninteractive stills or explicit animation from native state and/or imports, preserving supplied saved settings. | `./vsrRender [options] <input>...` |
| `vsrUsdInfo` | List the Variant Sets a USD Stage offers and their variants. | `./vsrUsdInfo <stage.usd>` |
| `vsrVolumeToNanoVDB` | Convert supported structured volume files to NanoVDB (`.vdb`) with optional quantization settings. | `./vsrVolumeToNanoVDB [options] <input_volume> <output.vdb>` |
| `vsrLua` | Lua scripting and REPL tool for scene import, manipulation, and rendering workflows. | `./vsrLua <script.lua> [args...]`, `./vsrLua -e "<lua code>"`, `./vsrLua -i` |

`vsrLua` is only built when `VSR_USE_LUA=ON`, and `vsrUsdInfo` only when
`VSR_USE_USD=ON`.

## `obj2header`

Converts OBJ triangle data into a header file containing:

- `vertex_position`
- `vertex_normal`
- `vertex_uv`

Usage:

```bash
./obj2header model.obj generated_model.h
```

## `vsrPrint`

Loads a `.vsr` file into `vsr::core::DataTree`, in whichever encoding it is
in, and prints it as the Text Encoding (see
[`src/vsr/core/DataTreeText.md`](../../vsr/core/DataTreeText.md)).

Usage:

```bash
./vsrPrint scene.vsr
```

## `vsrConvert`

Converts a Data Tree file between the Binary and Text Encodings. The input's
encoding is detected from its contents, never from its extension, and the
output is written in the other encoding unless `--text` or `--binary` forces
one. Forcing the input's own encoding re-saves the file in canonical form,
which is a cheap way to normalize a hand-edited text file. Both encodings
use the `.vsr` extension; a different suffix for text files is a naming
convention the tool neither needs nor checks.

Usage:

```bash
./vsrConvert cameras.vsr cameras_text.vsr      # binary -> text, ready to edit
./vsrConvert cameras_text.vsr cameras.vsr      # text -> binary
./vsrConvert --text edited.vsr canonical.vsr   # text -> canonical text
```

## `vsrAssemble`

Assembles a scene from the same scene-loading command line as the viewers (`-vsr`, `-gltf`, `-obj`, `-usd`, `-usd_variant`, `-l`/`--layer`,
...) and saves it as a Scene Archive instead of rendering it. No ANARI device
is loaded. The archive is written to `scene.vsr` unless `-o`/`--output`
names another file. If any input file does not exist, the tool lists every
missing one and exits without writing an archive.

A Scene Archive holds no animation-manager state, so an animated scene is
saved at its current frame without its animations.

Usage:

```bash
./vsrAssemble -l geometry -gltf model.glb -hdri env.hdr -o scene.vsr
```

## `vsrRender`

This is the **canonical CLI reference** for Vela's general headless renderer.
The [App documentation](../../vsr/app/README.md#resolved-offline-rendering)
explains the shared engine, adapter responsibilities and synchronous borrowing/
restoration contract; it is not a second CLI manual. Run `vsrRender --help`
without inputs for built-in discovery. Only long options listed here are
supported; `--` ends option parsing for paths beginning with `-`. Unknown
options, missing operands, invalid/conflicting settings and missing inputs fail
with nonzero status, without reading stdin. A render requires supplied scene
content; there is no implicit example scene.

```sh
./vsrRender [options] INPUT...
# Camera-less foreign data: explicitly request a view and convenient lighting.
./vsrRender model.obj --default-camera --default-lights \
  --width 320 --height 240 --samples 4 --output preview.png
```

Renders a still by default, or an explicit animation sequence, from foreign
imports and/or one validated Application Dump or Scene Archive, including
supported legacy representations and Binary/Text Encodings. Contents, not
extensions, determine input meaning. Dumps use their saved current animation
frame and supplied offline settings; scenes use frame 0 and baseline settings.
Required Archives and consumed render settings are validated strictly; failed
dump reconstruction never falls back to scene loading. Inert saved pose internals
are ignored, not rendered or converted. Animation or saved poses never implicitly
enable a sequence. A scene Camera is
required: select an exact name with `--camera NAME` (including numeric-only
names), or an unsigned decimal object index with `--camera-index N`. Duplicate
names fail. The selectors are mutually exclusive, and invalid explicit requests
never fall back to another
interpretation or Camera. Without either, use a valid saved selection or the sole
Camera. A stale saved selection may fall back only to the sole Camera;
missing/ambiguous selection fails with available names/indices and guidance.
For example, `--camera 12` selects the Camera named `12`, whereas
`--camera-index 12` selects object index 12. No prompts, implicit Cameras, pose
conversion or implicit lights are used. `--default-camera` explicitly
creates/selects a perspective Camera even when authored Cameras exist, and
cannot be combined with either selector.

Only explicit `--width`, `--height` and `--samples` override saved quality values.
Unspecified authored Camera parameters and compatible saved renderer settings
are retained. Lens settings belong to the Scene Camera; legacy duplicate
`offlineRendering.camera.apertureRadius` and `focusDistance` fields are ignored
with a warning, including nondefault values. Use Scene Camera parameters or
explicit `--camera-aperture`/`--camera-focus` flags instead. Defaults are selected-device/default renderer, 1024x768, 128 samples
and `vsrRender.png`. Supplied settings are validated; incompatible saved camera
overrides fail instead of silently changing the viewpoint. `--output` is an
exact PNG path for stills or a sequence pattern described below. Explicit output
overrides both saved output fields. Otherwise supplied saved directory/prefix
name a still as `directory/prefix.png`; absent directory means the current
directory and absent prefix means `vsrRender`. Stills omit one trailing
underscore from the saved prefix (`frame_` becomes `frame.png`); animation
prefixes are unchanged (`frame_0000.png`). A Scene Archive or dump with no
supplied output fields uses `vsrRender.png`, not default-initialized dump settings.
All still path text is literal, including braces.

`--device [subtype@]library` selects an ANARI Device Identifier. Omitted and
explicit `default` subtypes normalize identically (`helide` = `default@helide`).
Precedence is explicit flag, saved device, first nonempty `VSR_ANARI_LIBRARIES`
entry, `environment` when `ANARI_LIBRARY` is set, then `helide`. Loading or subtype
creation failure never falls back. `--renderer NAME` selects an advertised
renderer subtype. Changing the normalized device discards saved device-specific
parameters and uses the new device's `default` subtype unless explicitly selected;
changing subtype initializes its metadata defaults. Resets are clearly logged.
Compatible saved renderer subtype, values and enabled states remain unchanged.
Saved object-valued renderer parameters resolve through the Scene/RenderIndex;
stale references fail. This does not add CLI object/array construction syntax.

Repeat `--renderer-param name=value` for typed overrides. Numeric scalars use
complete decimal/scientific conversion; vectors use exactly the advertised number
of comma-separated numeric components without whitespace. Boolean values are
`true`, `false`, `1` or `0`; strings are literal, shell-quoted when needed.
The first `=` is the delimiter, and empty strings require metadata support.
Metadata validates types, numeric bounds and enumerated strings; ambiguous or
invalid conversions, unknown parameters and object/array/pointer/matrix/packed
constructions fail. Fixed-size integer (8/16/32/64-bit), float32/64, boolean,
string and numeric-vector (2/3/4-component) types are supported when advertised.
`--background R G B A`, `--ambient VALUE` and `--ambient-color R G B` are
shorthand assignments to `background`, `ambientRadiance` and `ambientColor`,
not a separate application path. Every assignment is validated, and later
assignments to the same name win (including shorthand). Absent controls do not
inject background/ambient defaults into saved settings.

Lighting is never injected based on input type or a zero Light count. Unlit
scenes stay unlit; HDRI/environment Lights and emissive Materials are preserved.
A scene with no conventional Lights may still render meaningfully through its
emissive Materials and the selected device's renderer behavior.

Repeat `--directional-light DX DY DZ R G B IRRADIANCE` to add directional scene
Lights. The direction is the world-space direction in which light travels,
not a position or target; it must be nonzero and is normalized. All seven
operands must be finite float values in range; RGB components and irradiance
must be nonnegative (zero is valid). Every occurrence adds a Light; none replace
or remove authored lighting. Malformed requests fail before rendering, even if
followed by a valid request. `--default-lights` explicitly adds the existing
convenient default-light setup once, whether or not Lights already exist, and
can be combined with directional additions. Neither option changes renderer
background or ambient parameters; those remain separate explicit controls.

```sh
# A camera-less import with explicitly requested lighting:
./vsrRender model.obj --default-camera --default-lights --samples 4
# Add two colored directional Lights to authored lighting:
./vsrRender scene.vsr --directional-light 0 0 -1 1 0.8 0.6 3 \
  --directional-light -1 -1 0 0.4 0.6 1 1 --output lit.png
# Emissive/native environment scenes do not require --default-lights:
./vsrRender emissive.vsr --output emission.png
```

Foreign-only inputs start from a fresh scene with no convenience Camera.
The native base always loads before imports, regardless of argument position;
multiple native bases fail. Foreign imports retain their relative argument
order, layer assignment and animation bindings. Native inputs remain native even
under an explicit importer selection. Import warnings remain visible and are
advisory: supported lossy conversions, such as a PBRT spherical Camera
approximated as perspective, may render successfully. Logged import errors,
exceptions, missing inputs, unavailable importers and imports producing no scene
content still fail the command. Valid transfer-function imports need not add
scene objects. No-content detection is a safety net, not proof of completeness;
warnings may describe approximated or omitted content, so review the diagnostics.
No generic Context parser/setup or implicit scene/light setup is used.

Common suffixes select importers automatically (`.obj`, `.ply`, `.gltf`/`.glb`,
`.pbrt`, `.usd`/`.usda`/`.usdc`, structured volumes and others). Use
`--importer NAME` for ambiguous or bespoke formats, or `--importer auto` to reset
suffix routing. `--help` lists all supported selections; optional dependencies
must be present in the build. `--layer NAME` applies to subsequent foreign
inputs (default `default`); saved activation of an existing layer is preserved.
`--vtu-property NAME` selects a property for subsequent VTU imports or volume
animation files. `--importer pointsbin` and `--importer volume-animation` group
subsequent files into one animation until importer/layer/property routing changes.
Re-select the importer to begin another sequence. Sequence imports preserve
bindings but render only frame 0 (or the native dump's saved frame) unless
`--frame` selects another still or `--animation` explicitly enables a sequence.
`--importer xf` applies a transfer function to following volume imports.
Foreign paths/layer/property names cannot contain `;`, reserved by the existing
import dispatch API.

### Quality and AOV visualization

`--width N`, `--height N` and `--samples N` require positive decimal integers in
range, with image/buffer arithmetic checked before rendering. Per-field precedence
is built-in defaults (1024x768, 128 samples), actually supplied Application Dump
fields, then explicit flags. Absent flags preserve saved values; explicit values
matching a default still override them. Partially saved settings keep defaults
only for their absent fields.

`--aov MODE` exposes the existing visualizations: `none` (color), `depth`,
`albedo`, `normal`, `edges`, `object-id`, `primitive-id` and `instance-id`.
Explicit `none` disables a saved visualization. `--depth-min VALUE` and
`--depth-max VALUE` override each depth bound independently (defaults 0 and 1).
Finite increasing bounds with a finite representable span are required.
`--edge-invert true|false|1|0` controls edges (default false); explicit false/0
replaces saved true. Absent options preserve saved AOV fields, and repeats are
last-wins. Settings are validated even if their visualization is currently disabled.

The shared engine requests the appropriate frame channel (object IDs for edges)
and requires device extension support for optional channels. Mapped buffer data,
dimensions and types are validated before visualization or writing, not silently
replaced by a misleading image. Device IDs and sentinel values are visualized as
provided; unassigned instance IDs can therefore yield a uniform visualization.
Output is the existing visualization PNG, not raw channels, HDR or a new algorithm.

```sh
./vsrRender scene.vsr --width 320 --height 240 --samples 4 \
  --aov depth --depth-min 0 --depth-max 10 --output depth.png
./vsrRender saved.vsr --aov edges --edge-invert false --output edges.png
./vsrRender saved.vsr --aov none --output color.png
# Requires an installed device advertising albedo support, such as ospray:
./vsrRender scene.vsr --device ospray --aov albedo --output albedo.png
```

### Frame selection and sequence output

`--frame N` selects one existing zero-based timeline frame, overriding the saved
current frame without enabling a sequence. `--animation` explicitly renders all
supported animated content (geometry, Arrays, transforms and Cameras). It requires
at least one animation binding; a still input cannot fabricate a sequence.
`--frame` and `--animation` conflict. Saved poses never enable animation.

Without `--frames`, animation uses the saved render subset when enabled, otherwise
the full existing Animation Manager timeline, honoring the saved stride.
`--animation --frames START:END[:STEP]` replaces saved subset and stride settings.
Indices are nonnegative decimal integers in the existing timeline, end is
inclusive, and omitted step is 1. Step must be positive; end need not fall on the
step grid. Malformed, overflowing, reversed and out-of-timeline selections fail
before rendering. Selection never changes total frames, duration or time mapping;
saved legacy frame counts do not retime the animation.

A sequence `--output` must end in `.png` and contain **exactly one** literal
`{frame:04}` placeholder, with no other braces. All other path text is literal;
this is not a general formatting language. The placeholder becomes the actual
timeline index with a minimum of four zero-padded digits (10000 stays 10000).
Malformed patterns and duplicate selected destinations (including filesystem
aliases) fail before rendering. A one-frame animation subrange still requires
sequence naming.

Without an explicit sequence output, supplied saved output directory/prefix are
used, with four-digit indices appended to the prefix. Absent directory means the
current directory; absent prefix means `vsrRender_`, not the persistence object's
default-initialized prefix. With neither supplied, names are `vsrRender_0000.png`,
`vsrRender_0001.png`, etc. Explicit output overrides both saved fields.

The CLI preflights the **complete selected job** before writing any image and
creates missing destination directories. Existing selected outputs refuse the
entire job unless `--overwrite` explicitly permits replacement: it never silently
writes only the nonexisting subset. Unselected files are untouched. Even with
`--overwrite`, directories, dangling links and other non-regular destination
types fail, as do path inspection/directory creation errors and destinations
that alias the same file. Regular-file symbolic links are followed; hard-link
collisions are also rejected. Filesystem changes after preflight or runtime
write failures can still fail the job; partial files may remain. This is not an
atomic sequence transaction. Failures identify the destination and return a
nonzero exit status. PNG orientation, sRGB color output and saved AOV
visualization are unchanged. This CLI policy does not change UI overwrite behavior.

```sh
./vsrRender saved.vsr --frame 7 --output preview.png
./vsrRender saved.vsr --animation
# Inclusive selection visits timeline frames 2, 5, 8, without retiming:
./vsrRender saved.vsr --animation --frames 2:10:3 \
  --output 'renders/shot_{frame:04}.png'
# Explicit permission to replace the selected existing images:
./vsrRender saved.vsr --animation --frames 2:10:3 \
  --output 'renders/shot_{frame:04}.png' --overwrite
./vsrRender saved.vsr --animation --frames 7:7 --output 'shot_{frame:04}.png'
```

Animation advances before authoritative Camera overrides. Each frame starts
fresh accumulation. Generated Cameras fit once at the global first selected
frame and stay fixed; later motion may leave their view.

### Optional MPI frame distribution

Configure with `-DVSR_USE_MPI=ON` to enable MPI in `vsrRender`. The same executable
also runs directly as a single-rank command. MPI-off builds and the shared App
engine/UI caller do not depend on MPI.

```sh
mpiexec -n 2 ./vsrRender saved.vsr --animation --frames 20:27:2 \
  --default-camera --output 'renders/shot_{frame:04}.png'
```

Use identical inputs/options, device availability and a shared output filesystem
on every rank. Each rank resolves the complete global job; rank 0 coordinates
filesystem preflight/directory creation before any rank starts writing. Selected
frames are distributed **by position in the selection**, not timeline index
modulo rank count: the example assigns frames 20/24 to rank 0 and 22/26 to rank 1.
Every frame has the same destination as a serial invocation, with no rank suffix.
Generated Cameras fit at global frame 20 on both rendering ranks, not each rank's
first local frame, and then remain fixed.

Only rank 0 renders/writes a still. Ranks with no assigned sequence frames are
idle for rendering but participate in status/completion handling. Per-rank logs
identify actual frame indices and local progress; global completion is reported
only when all ranks succeed. Any rank's ordinary argument/input/device/preflight/
render/write error produces collective nonzero completion and a diagnostic from
the lowest failing rank. Fatal MPI operations or resource exhaustion use MPI
abort rather than trying to continue an unsupported runtime state. Successful and
supported failed paths release resources and finalize MPI. Partial images after
a runtime failure may remain; no retries or atomic sequence publication are
provided. MPI distributes whole frames, **not samples within one frame**.

### Camera controls

Camera-less formats such as OBJ require a Camera in the native base/another
import, or explicit `--default-camera`. Generation evaluates the selected frame
before fitting scene bounds once, looks along -Z with +Y up, uses a 40-degree
vertical FOV and a 5% distance margin, and accounts for output aspect.
Empty/nonfinite bounds fail. The view stays fixed; later animated motion may
leave it. Generation does not add lighting or use saved poses.

Both authored and generated Cameras accept `--camera-position X Y Z`,
`--camera-look-at X Y Z` (world-space target), and `--camera-up X Y Z`.
`--camera-fovy DEGREES` requires perspective; `--camera-height VALUE` requires
orthographic. `--camera-aperture VALUE` and `--camera-focus VALUE` require lens
support from the subtype/device. Invalid geometry/numbers or unsupported controls
fail rather than being ignored. Authored Scene Camera values precede explicit
controls; repeats are last-wins. Controls remain authoritative after animation, while all
unspecified authored parameters/subtype are preserved.

```bash
./vsrRender model.obj --default-camera --width 320 --height 240 --samples 4
./vsrRender state_file.vsr --output saved-still.png
./vsrRender scene.vsr --device default@helide --renderer default \
  --renderer-param mode=hitSurface --background 0 0 1 1 --samples 4
# Optional CPU ospray library; reset saved settings before applying overrides:
./vsrRender state_file.vsr --device ospray --renderer scivis \
  --renderer-param background=0,0,0,1 --renderer-param shadows=false
./vsrRender scene.data --camera 'Main Camera' --width 320 --height 240 \
  --samples 4 --output preview.png
./vsrRender scene.vsr --camera 'Main Camera' --camera-position 0 0 3 \
  --camera-look-at 0 0 0 --camera-up 0 1 0 --samples 4 --output viewpoint.png
./vsrRender authored-camera.pbrt --width 320 --height 240 --samples 4 \
  --output imported.png
./vsrRender base.vsr --layer detail --importer obj detail.obj \
  --width 320 --height 240 --samples 4 --output augmented.png
# The base can also appear after the foreign input:
./vsrRender --layer detail detail.obj base.vsr --output augmented.png
./vsrRender base.vsr --layer motion --importer volume-animation \
  density0.raw density1.raw --output first-frame.png
./vsrRender --help
```

## `vsrUsdInfo`

Lists the Variant Sets a USD Stage offers and the variants each one authors,
which are the names `-usd_variant <set>=<variant>` accepts in `vsrViewer`.
The Stage opens with no payload loaded, so this is quick even
for a large Stage, but a Variant Set that only a payload authors is not
listed. Built only when `VSR_USE_USD=ON`.

Usage:

```bash
./vsrUsdInfo shot.usda
# geometry: off render
# material: anim off render renderLow renderXLow
# rig: off skel
```

## `vsrVolumeToNanoVDB`

Converts a volume file to NanoVDB and supports handling undefined values,
quantization precision, and dithering.

Usage:

```bash
./vsrVolumeToNanoVDB [options] <input_volume> <output.vdb>
```

Options:

- `--undefined <value>`, `-u <value>` skip voxels matching an undefined value
- `--precision <type>`, `-p <type>` choose `fp4|fp8|fp16|fpn|half|float32`
- `--dither`, `-d` enable quantization dithering

Supported input formats include `.raw`, `.vti`, `.vtu`, `.mhd`, `.hdf5`, and
`.nvdb`.

Example:

```bash
./vsrVolumeToNanoVDB --undefined 0.0 --precision fp8 --dither input.mhd output.vdb
```

## `vsrLua`

Lua front end for scripted VSR workflows. It pre-creates a scene named `scene`
for scripts and provides:

- Batch script execution
- Inline code execution (`-e`)
- Interactive REPL (`-i`)

Usage:

```bash
./vsrLua script.lua [args...]
./vsrLua -e "print(scene:numberOfObjects(vsr.GEOMETRY))"
./vsrLua -i
```

For miniScene export without rendering, use the existing Lua/I/O API (requires
`VSR_USE_LUA=ON` and `VSR_USE_MINISCENE=ON`):

```sh
./vsrLua -e 'vsr.io.importOBJ(scene, animationMgr, "model.obj"); assert(vsr.io.exportSceneToMiniScene(scene, "model.mini"))'
```

See [ADR 0042](../../../docs/adr/0042-export-miniscene-in-the-form-haystack-consumes.md)
for export limits and the [scripting reference](../../vsr/scripting/README.md)
for other import/export operations. Scene export is not a render mode.

In script mode, extra CLI arguments are passed to Lua in the `arg` table.
