## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsr_ui_imgui)

project_add_library(OBJECT)

project_sources(PRIVATE
  modals/AppSettingsDialog.cpp
  modals/BlockingTaskModal.cpp
  modals/ExportNanoVDBFileDialog.cpp
  modals/ObjectFileDialog.cpp
  modals/OfflineRenderModal.cpp
  modals/ImportFileDialog.cpp
  modals/Modal.cpp
  modals/CuttingPlaneDialog.cpp
  modals/VorticityDialog.cpp
  windows/Animations.cpp
  windows/Timeline.cpp
  windows/BaseViewport.cpp
  windows/CameraPoses.cpp
  windows/DatabaseEditor.cpp
  windows/IsosurfaceEditor.cpp
  windows/LayerTree.cpp
  windows/Log.cpp
  windows/MultiDeviceViewport.cpp
  windows/ObjectEditor.cpp
  windows/Terminal.cpp
  windows/TransferFunctionEditor.cpp
  windows/Viewport.cpp
  windows/Window.cpp
  Application.cpp
  ArrayPreview.cpp
  ExtensionManager.cpp
  vsr_ui_imgui.cpp
)

if(VSR_USE_LUA)
  project_link_libraries(PRIVATE vsr_lua vela_ext_sol2)
  project_compile_definitions(PRIVATE VSR_USE_LUA)
endif()

project_link_libraries(
PUBLIC
  vsr_app
  vsr_core
  vsr_io
  vsr_rendering
  vela_ext_imguizmo
  vela_ext_imoguizmo
  vela_ext_imgui_sdl
  vela_ext_stb_image
  vela_ext_tinyexr
)

project_include_directories(
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)
