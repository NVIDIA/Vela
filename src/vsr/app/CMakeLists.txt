## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsr_app)

project_add_library(STATIC)
project_sources(PRIVATE
  ANARIDeviceManager.cpp
  ApplicationDump.cpp
  Context.cpp
  LegacyApplicationContext.cpp
  renderAnimationSequence.cpp
)
project_link_libraries(PUBLIC vsr_scene vsr_io vsr_rendering)
project_include_directories(PUBLIC ${CMAKE_CURRENT_LIST_DIR})
