# Third-Party Notices

This project includes third-party open-source software. The components below are
vendored in this repository under `external/`. Each component remains under its own
license; the notices below are attribution only and are not a substitute for those
licenses.

## Vendored in this repository

fmt
Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
Licensed under the MIT License
Path: `external/vela_fmtlib` (upstream license text preserved at `external/vela_fmtlib/LICENSE`)

OpenGL Mathematics (GLM)
Copyright (c) 2005 - G-Truc Creation
Dual-licensed under The Happy Bunny License (Modified MIT License) OR the MIT
License, at your option. Note that The Happy Bunny License adds a non-binding
restriction regarding military use; the MIT option is available without it.
Path: `external/vela_glm` (upstream license text preserved at `external/vela_glm/LICENSE`)

NanoVDB (OpenVDB Project)
Copyright Contributors to the OpenVDB Project
Licensed under the Apache License 2.0
Path: `external/vela_nanovdb`

Catch2
Copyright (c) 2021 Two Blue Cubes Ltd. All rights reserved.
Licensed under the Boost Software License 1.0
Path: `external/vela_catch`

stb_image / stb_image_write / stb_image_resize
Copyright (c) Sean Barrett
Public domain (Unlicense) or MIT License, at your option
Path: `external/vela_stb_image`

TinyEXR
Copyright (c) 2014 - 2020, Syoyo Fujita and many contributors.
Licensed under the BSD 3-Clause License
Path: `external/vela_tinyexr`

TinyGLTF
Copyright (c) 2015 - Present Syoyo Fujita, Aurelien Chatelain and many contributors.
Licensed under the MIT License
Path: `external/vela_tinygltf`

tinyobjloader
Copyright (c) 2012-2018 Syoyo Fujita and many contributors.
Licensed under the MIT License
Path: `external/vela_tinyobjloader`

tinyply
Copyright (c) Dimitri Diakopoulos
Public domain (Unlicense)
Path: `external/vela_tinyply`

MikkTSpace
Copyright (C) 2011 by Morten S. Mikkelsen
Licensed under the zlib License
Path: `external/vela_mikktspace`

imoguizmo
Copyright (c) 2022 Lukas Lipp
Licensed under the MIT License
Path: `external/vela_imoguizmo`

NVIDIA FLIP
Copyright (c) 2020-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
Licensed under the BSD 3-Clause License
Path: `external/vela_flip`

agx (Animated Geometry eXchange)
Copyright 2025 Jefferson Amstutz
Licensed under the Apache License 2.0
Upstream: https://github.com/jeffamstutz/agx
Path: `external/vela_agx` (upstream license text preserved at `external/vela_agx/LICENSE`)

## Embedded in project source

Cousine (font)
Copyright (c) Steve Matteson
Licensed under the Apache License 2.0
Path: `src/vsr/ui/imgui/fonts/vsr_font_cousine_regular.h` — the `Cousine-Regular.ttf`
font data as distributed with Dear ImGui, compressed to a C array with the upstream
`binary_to_compressed_c` tool.

## Downloaded at build time

The following dependencies are not vendored in this repository. The build fetches
them from the sources listed below; each remains under its own license.

Dear ImGui — https://github.com/ocornut/imgui (MIT License)
SDL — https://github.com/libsdl-org/SDL (zlib License)
ImGuizmo — https://github.com/CedricGuillemet/ImGuizmo (MIT License)
imnodes — https://github.com/Nelarius/imnodes (MIT License)
Lua — https://www.lua.org/ (MIT License)
sol2/sol3 — https://github.com/ThePhD/sol2 (MIT License)
zlib — https://github.com/madler/zlib (zlib License)
