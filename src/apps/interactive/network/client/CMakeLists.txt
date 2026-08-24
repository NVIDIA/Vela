## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsrTestClient)
project_add_executable(vsrTestClient.cpp)
project_link_libraries(vsr_network vela_ext_stb_image)

project(vsrRemoteViewer)
project_add_executable(
  NetworkUpdateDelegate.cpp
  RemoteViewport.cpp
  vsrRemoteViewer.cpp
)
project_link_libraries(vsr_ui_imgui vsr_network)
