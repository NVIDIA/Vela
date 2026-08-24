## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsr_lua)

project_add_library(STATIC)

project_sources(
PRIVATE
  LuaContext.cpp
  LuaBindings.cpp
  bindings/CoreBindings.cpp
  bindings/MathBindings.cpp
  bindings/ObjectBindings.cpp
  bindings/LayerBindings.cpp
  bindings/IOBindings.cpp
  bindings/RenderBindings.cpp
  bindings/ParameterHelpers.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>
)

project_link_libraries(
PUBLIC
  vsr_scene
  vsr_io
  vsr_rendering
PRIVATE
  $<BUILD_INTERFACE:vela_ext_sol2>
  $<BUILD_INTERFACE:vela_ext_stb_image>
)

project_compile_definitions(
PUBLIC
  VSR_USE_LUA
PRIVATE
  VSR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
  set_source_files_properties(
    bindings/CoreBindings.cpp
    bindings/ObjectBindings.cpp
    PROPERTIES COMPILE_OPTIONS "/bigobj"
  )
endif()
