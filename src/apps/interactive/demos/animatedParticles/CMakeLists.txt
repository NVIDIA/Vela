## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

if (NOT VSR_USE_CUDA)
  return()
endif()

find_package(vela_glm MODULE REQUIRED)

project(vsrDemoAnimatedParticles)
project_add_executable(
  particle_system.cu
  SimulationControls.cpp
  vsrDemoAnimatedParticles.cpp
)
project_link_libraries(vsr_ui_imgui vela::glm)
set_source_files_properties(
  particle_system.cu
  PROPERTIES
    COMPILE_OPTIONS "--extended-lambda;--expt-relaxed-constexpr;-Xcudafe=--diag_suppress=20012;-Wno-deprecated-gpu-targets"
    LANGUAGE CUDA
)
