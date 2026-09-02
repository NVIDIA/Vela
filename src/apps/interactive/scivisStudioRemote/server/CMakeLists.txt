## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# Everything but main(), so vsrTests can drive a real StudioServer
# in-process.
add_library(vsr_scivis_studio_server_core OBJECT
  DataRoots.cpp
  ProjectOpDispatcher.cpp
  RemoteBrowse.cpp
  ServerOptions.cpp
  ServerPushDelegate.cpp
  ServerTaskRunner.cpp
  StudioServer.cpp
  ViewportPasses.cpp
)

target_include_directories(vsr_scivis_studio_server_core
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(vsr_scivis_studio_server_core
PUBLIC
  vsr_app
  vsr_scivis_studio_protocol
)

add_executable(scivisStudioServer scivisStudioServer.cpp)

# OBJECT libraries do not hand their object files on transitively, so the
# executable names every one it depends on.
target_link_libraries(scivisStudioServer
PRIVATE
  vsr_scivis_studio_server_core
  vsr_scivis_studio_protocol
  vsr_scivis_studio_model
)
