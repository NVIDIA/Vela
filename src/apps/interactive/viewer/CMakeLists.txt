## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsrViewer)
project_add_executable(vsrViewer.cpp)
project_link_libraries(PUBLIC vsr_ui_imgui)
if(VSR_USE_LUA)
  project_compile_definitions(PRIVATE VSR_USE_LUA)
endif()