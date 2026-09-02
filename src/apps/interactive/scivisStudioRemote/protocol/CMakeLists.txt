## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_library(vsr_scivis_studio_protocol OBJECT
  BrowseMessages.cpp
  FrameCodec.cpp
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
  StudioEndpoint.cpp
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

if (VSR_USE_TURBOJPEG)
  find_package(libjpeg-turbo CONFIG REQUIRED)
  target_link_libraries(vsr_scivis_studio_protocol
  PUBLIC
    libjpeg-turbo::turbojpeg
  )
  target_compile_definitions(vsr_scivis_studio_protocol
  PUBLIC
    VSR_USE_TURBOJPEG
  )
endif()
