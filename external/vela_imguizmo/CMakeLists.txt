## SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_imguizmo LANGUAGES CXX)

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/CedricGuillemet/ImGuizmo/archive/105e4fde45e4e74aae8973d98750820ef7aca7e2.zip
  MD5 f0ad228d7d7800356c67f7bc30a696d5
)

project_add_library(STATIC)

project_sources(
PRIVATE
  ${vela_ext_imguizmo_LOCATION}/src/ImGuizmo.cpp
)

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${vela_ext_imguizmo_LOCATION}/src>
)

project_link_libraries(
PRIVATE
  anari::helium
  vela_ext_imgui_sdl
)
