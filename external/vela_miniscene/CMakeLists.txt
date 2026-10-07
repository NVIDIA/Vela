## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_miniscene LANGUAGES CXX)

# miniScene has no releases, so pin a commit. Only the core library is built
# here: miniScene's own CMakeLists always adds its importers and tools, can
# write CMAKE_BUILD_TYPE and output directories, and defines an stb_image
# target -- none of which Vela needs (see docs/adr/0042).
anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/ingowald/miniScene/archive/4eee09bd95d91b8b9e29eeecb1df6053cce84590.zip
  MD5 445538f4da1c4b952bc9cd14d21729a9
)

project_add_library(STATIC
  ${vela_ext_miniscene_LOCATION}/miniScene/Scene.cpp
  ${vela_ext_miniscene_LOCATION}/miniScene/Serialized.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${vela_ext_miniscene_LOCATION}>
)

# miniScene's parallel_for only uses TBB when OWL_HAVE_TBB is set; keep it
# serial so the library needs nothing beyond its own headers.
project_compile_definitions(PUBLIC OWL_DISABLE_TBB)

set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Suppress warnings from miniScene code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  project_compile_options(PRIVATE -w)
elseif(MSVC)
  project_compile_options(PRIVATE /W0)
endif()
