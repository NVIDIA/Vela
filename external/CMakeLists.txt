## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(vela_agx)
add_subdirectory(vela_catch)
add_subdirectory(vela_flip)
add_subdirectory(vela_fmtlib)
add_subdirectory(vela_imnodes)
add_subdirectory(vela_imoguizmo)
add_subdirectory(vela_mikktspace)
add_subdirectory(vela_nanovdb)
add_subdirectory(vela_stb_image)
add_subdirectory(vela_tinyexr)
add_subdirectory(vela_tinygltf)
add_subdirectory(vela_tinyobjloader)
add_subdirectory(vela_tinyply)
add_subdirectory(vela_zlib EXCLUDE_FROM_ALL)

if(VSR_BUILD_UI_LIBRARY)
  add_subdirectory(vela_imgui_sdl)
  add_subdirectory(vela_imguizmo)
endif()

if(VSR_USE_LUA)
  add_subdirectory(vela_lua)
  add_subdirectory(vela_sol2 EXCLUDE_FROM_ALL)
endif()
