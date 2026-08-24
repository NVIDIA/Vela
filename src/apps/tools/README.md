## Command-Line Tools

This directory contains utility executables for data conversion, scene inspection,
headless rendering, and scripting.

## Tools Overview

| Tool | Purpose | Usage |
| --- | --- | --- |
| `obj2header` | Convert a Wavefront OBJ mesh into a generated C/C++ header with packed vertex arrays. | `./obj2header <file.obj> <outfile.h>` |
| `vsrPrint` | Load a serialized `.vsr` file and print its `DataTree` contents. | `./vsrPrint <file.vsr>` |
| `vsrRender` | Offline render from a saved `.vsr` state file (including render settings and camera/animation data). | `./vsrRender <state_file.vsr>` |
| `vsrOffline` | Headless renderer with direct CLI control over imports, camera, lights, renderer, and output. | `./vsrOffline [options]` |
| `vsrVolumeToNanoVDB` | Convert supported structured volume files to NanoVDB (`.vdb`) with optional quantization settings. | `./vsrVolumeToNanoVDB [options] <input_volume> <output.vdb>` |
| `vsrLua` | Lua scripting and REPL tool for scene import, manipulation, and rendering workflows. | `./vsrLua <script.lua> [args...]`, `./vsrLua -e "<lua code>"`, `./vsrLua -i` |

`vsrLua` is only built when `VSR_USE_LUA=ON`.

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

Loads a `.vsr` file into `vsr::core::DataTree` and prints the tree structure.

Usage:

```bash
./vsrPrint scene.vsr
```

## `vsrRender`

Renders from a saved VSR state file. It loads scene content and offline render
settings from the file and writes PNG frames named `vsrRender_0000.png`,
`vsrRender_0001.png`, etc. If the scene has keyframed animation, it renders one
image per animation frame.

Usage:

```bash
./vsrRender state_file.vsr
```

## `vsrOffline`

General-purpose headless renderer. It uses the same importer command-line model
as `vsr::app::Context` and adds offline-rendering controls (resolution, samples,
camera, lights, and output).

Usage:

```bash
./vsrOffline [options]
```

Common options:

- `-w, --width <int>` frame width (default `1024`)
- `-h, --height <int>` frame height (default `768`)
- `-s, --samples <int>` samples per pixel (default `128`)
- `-o, --output <file>` output image path (default `vsrOffline.png`)
- `--lib <name>` ANARI library
- `--renderer <name>` renderer subtype (default `default`)
- `--camera <name-or-index>` select a scene camera by exact name or object index
- `--campos <x y z>`, `--lookpos <x y z>`, `--upvec <x y z>`, `--fovy <float>`
- `--aperture <float>`, `--focus <float>`
- `--bg-color <r g b a>`, `--no-bg`
- `--ambient <float>`, `--ambient-color <r g b>`
- `--dir-light <dx dy dz> <r g b> <intensity>`

If `--camera` is omitted, or if the requested camera is invalid, `vsrOffline`
prints the available scene cameras and prompts on stdin for a selection. If the
scene has no cameras, it creates a default perspective camera and frames it
from the scene bounds. `--camera` is mutually exclusive with manual camera
override flags (`--campos`, `--lookpos`, `--upvec`, `--fovy`).

Use `-vsr <file>` to load a Scene Archive. Foreign-format importer flags
include `-gltf`, `-obj`, `-ply`, `-volume`, `-hdri`, `-silo`, `-usd`,
`-usd_mtlx`,
`-assimp`, `-axyz`, `-e57xyz`, `-pdb`, `-swc`, `-trk`, `-nbody`, and
`-l`/`--layer`.

Example:

```bash
./vsrOffline -gltf scene.glb -w 1920 -h 1080 -s 256 -o render.png
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

In script mode, extra CLI arguments are passed to Lua in the `arg` table.
