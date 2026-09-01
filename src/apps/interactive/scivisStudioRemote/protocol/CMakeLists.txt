## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_library(vsr_scivis_studio_protocol OBJECT
  BrowseMessages.cpp
  FrameMessages.cpp
  PayloadCommon.cpp
  PlaybackMessages.cpp
  ProjectOpReply.cpp
  ProjectRequests.cpp
  ProjectSnapshot.cpp
  SceneEditMessages.cpp
  SceneMessages.cpp
  SessionMessages.cpp
  ShotRigRequests.cpp
  StudioProtocol.cpp
  TaskMessages.cpp
  ViewportMessages.cpp
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
