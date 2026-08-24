## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsrDemoViskores)
project_add_executable(vsrDemoViskores.cpp NodeEditor.cpp NodeInfoWindow.cpp)
find_package(viskores_graph 1.1.0 REQUIRED)
project_link_libraries(
  vsr_ui_imgui
  vela_ext_imnodes
  vsr_core
  vsr_device
  viskores_graph::viskores_graph
)
project_compile_definitions(PRIVATE -DIMGUI_DEFINE_MATH_OPERATORS)
