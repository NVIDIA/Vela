## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsrMPIViewer)
project_add_executable(
  DistributedSceneController.cpp
  DistributedViewport.cpp
  mpiViewer.cpp
)
project_link_libraries(PUBLIC vsr_ui_imgui vsr_mpi)
