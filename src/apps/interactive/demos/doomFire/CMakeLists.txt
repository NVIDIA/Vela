## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

if (NOT VSR_USE_CUDA)
  return()
endif()

project(vsrDemoDoomFire)
project_add_executable(doomFire3D.cu FireControls.cpp vsrDemoDoomFire.cpp)
project_link_libraries(vsr_ui_imgui)
