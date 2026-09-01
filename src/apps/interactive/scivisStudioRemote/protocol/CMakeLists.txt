## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_library(vsr_scivis_studio_protocol OBJECT
  FrameMessages.cpp
  PayloadCommon.cpp
  ProjectRequests.cpp
  SessionMessages.cpp
  StudioProtocol.cpp
)

target_include_directories(vsr_scivis_studio_protocol
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(vsr_scivis_studio_protocol
PUBLIC
  vsr_network
  vsr_scivis_studio_model
)
