## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# UI-agnostic client core: connection state machine, Structural Mirror
# delegate, Project Replica, Project Ops and Server Task tracking. No vsr_ui_imgui dependency so it stays unit
# testable; the scivisStudioClient executable builds on it below.
add_library(vsr_scivis_studio_client_core OBJECT
  ArchiveRenameFollowUp.cpp
  MirrorUpdateDelegate.cpp
  ProjectOps.cpp
  ReplicaView.cpp
  ServerConnection.cpp
)

target_include_directories(vsr_scivis_studio_client_core
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(vsr_scivis_studio_client_core
PUBLIC
  vsr_scivis_studio_protocol
)

# The thin client executable: ImGui UI over the client core.
add_executable(scivisStudioClient
  Application.cpp
  EditorContext.cpp
  RemoteBrowseDialog.cpp
  StudioViewport.cpp
  UICommon.cpp
  scivisStudioClient.cpp
  modals/AddFileAnimationDatasetDialog.cpp
  modals/AddStaticDatasetDialog.cpp
  modals/ProjectLocationDialog.cpp
  windows/CameraRigEditor.cpp
  windows/DatasetEditor.cpp
  windows/EditorWindow.cpp
  windows/LightRigEditor.cpp
  windows/ProjectWindow.cpp
  windows/ShotEditor.cpp
  windows/TaskPanel.cpp
)

# OBJECT libraries only contribute their objects to direct linkers, so the
# core's own OBJECT dependencies are named here as well.
target_link_libraries(scivisStudioClient
PRIVATE
  vsr_scivis_studio_client_core
  vsr_scivis_studio_protocol
  vsr_scivis_studio_model
  vsr_ui_imgui
)
