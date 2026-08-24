## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

if (NOT VSR_USE_CUDA)
  return()
endif()

project(vsrDemoAnimatedVolume)
project_add_executable(jacobi3D.cu SolverControls.cpp vsrDemoAnimatedVolume.cpp)
project_link_libraries(vsr_ui_imgui)
