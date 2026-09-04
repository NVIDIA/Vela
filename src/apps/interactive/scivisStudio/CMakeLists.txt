## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_library(vsr_scivis_studio_model OBJECT
  CameraRig.cpp
  DataNodeFields.cpp
  Dataset.cpp
  DatasetIO.cpp
  LightRig.cpp
  LightRigIO.cpp
  Project.cpp
  ProjectAssetTransaction.cpp
  ProjectContext.cpp
  ProjectOpenPersistence.cpp
  ProjectPersistence.cpp
  ProjectSerialization.cpp
  RenderShot.cpp
  RenderShotCLI.cpp
  Shot.cpp
  StudioCLI.cpp
)

target_include_directories(vsr_scivis_studio_model
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(vsr_scivis_studio_model
PUBLIC
  vsr_app
  vsr_core
  vsr_io
  vsr_rendering
  vsr_scene
)

set(SCIVIS_STUDIO_DEFAULT_LAYOUT_FILE
  "${CMAKE_CURRENT_LIST_DIR}/default_ui_layout.txt")

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SCIVIS_STUDIO_DEFAULT_LAYOUT_FILE}")

file(READ
  "${SCIVIS_STUDIO_DEFAULT_LAYOUT_FILE}"
  SCIVIS_STUDIO_DEFAULT_LAYOUT)

configure_file(
  DefaultLayout.h.in
  "${CMAKE_CURRENT_BINARY_DIR}/DefaultLayout.h"
  @ONLY)

add_executable(scivisStudio
  Application.cpp
  modals/AddFileAnimationDatasetDialog.cpp
  modals/AddStaticDatasetDialog.cpp
  modals/ProjectLocationDialog.cpp
  scivisStudio.cpp
  windows/CameraRigEditor.cpp
  windows/DatasetEditor.cpp
  windows/LightRigEditor.cpp
  windows/ProjectWindow.cpp
  windows/ShotEditor.cpp
)

target_include_directories(scivisStudio
PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(scivisStudio
PRIVATE
  vsr_scivis_studio_model
  vsr_ui_imgui
)

add_executable(scivisStudioRenderShot
  scivisStudioRenderShot.cpp
)

target_link_libraries(scivisStudioRenderShot
PRIVATE
  vsr_scivis_studio_model
)

add_executable(scivisStudioCLI
  scivisStudioCLI.cpp
)

target_link_libraries(scivisStudioCLI
PRIVATE
  vsr_scivis_studio_model
)
