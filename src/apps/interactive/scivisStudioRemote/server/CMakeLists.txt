## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# Everything but main(), so vsrTests can drive the server in-process.
add_library(vsr_scivis_studio_server_core OBJECT
  ServerOptions.cpp
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
