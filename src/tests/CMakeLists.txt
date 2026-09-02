## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

if (NOT BUILD_TESTING)
  return()
endif()

project(vsrTests LANGUAGES CXX)
project_add_executable(
  test_App.cpp
  test_AnimationManager.cpp
  test_AnimationArchive.cpp
  test_AnimationManagerArchive.cpp
  test_ArchiveCompatibility.cpp
  test_CameraRendererArchive.cpp
  test_Algorithms.cpp
  test_AnyArray.cpp
  test_Array.cpp
  test_ComponentSerialization.cpp
  test_DataPath.cpp
  test_DataTree.cpp
  test_FlatMap.cpp
  test_Forest.cpp
  test_Geometry.cpp
  test_ImageImport.cpp
  test_Importers.cpp
  test_LayerSubtreeArchive.cpp
  test_Manipulator.cpp
  test_ObjectPool.cpp
  test_Material.cpp
  test_Math.cpp
  test_Object.cpp
  test_ObjectArchive.cpp
  test_ObjectUsePtr.cpp
  test_Parameter.cpp
  test_Procedural.cpp
  test_RenderIndex.cpp
  test_Scene.cpp
  test_SceneArchive.cpp
  test_Token.cpp
  test_UsdImport.cpp
  test_UsdImport_animation.cpp
  test_UsdImport_geometry.cpp
  test_UsdImport_instancing.cpp
  test_UsdImport_lights.cpp
  test_UsdImport_materials.cpp
  test_UsdImport_materials_portable.cpp
  test_UsdImport_subsets.cpp
  test_UsdImport_volumes.cpp
)
if (TARGET vsr_scivis_studio_model)
  target_sources(${PROJECT_NAME} PRIVATE
    test_ProjectAssetTransaction.cpp
    test_SciVisStudio.cpp
  )
endif()
if (TARGET vsr_network)
  target_sources(${PROJECT_NAME} PRIVATE test_NetworkChannel.cpp)
endif()
if (TARGET vsr_scivis_studio_client_core)
  target_sources(${PROJECT_NAME} PRIVATE test_StudioClientConnection.cpp)
endif()
if (TARGET vsr_scivis_studio_server_core)
  target_sources(${PROJECT_NAME} PRIVATE test_StudioServer.cpp)
endif()
if (TARGET vsr_scivis_studio_client_core AND TARGET vsr_scivis_studio_server_core)
  target_sources(${PROJECT_NAME} PRIVATE test_StudioRemoteE2E.cpp)
endif()
if (TARGET vsr_scivis_studio_test_client_core AND TARGET vsr_scivis_studio_server_core)
  target_sources(${PROJECT_NAME} PRIVATE test_StudioTestClient.cpp)
endif()
if (TARGET vsr_scivis_studio_protocol)
  target_sources(${PROJECT_NAME} PRIVATE
    test_StudioProtocol.cpp
    test_StudioProtocol_FrameCodec.cpp
    test_StudioProtocol_Frames.cpp
    test_StudioProtocol_ProjectRequests.cpp
    test_StudioProtocol_Scene.cpp
    test_StudioProtocol_ShotRigRequests.cpp
    test_StudioProtocol_Tasks.cpp
    test_StudioProtocol_Viewport.cpp
  )
endif()
project_link_libraries(
PRIVATE
  vsr_algorithms
  vsr_animation
  vsr_app
  vsr_io
  vsr_rendering
  vsr_scene
  vela_ext_catch2
)
if (TARGET vsr_scivis_studio_model)
  target_link_libraries(${PROJECT_NAME} PRIVATE vsr_scivis_studio_model)
endif()
if (TARGET vsr_network)
  target_link_libraries(${PROJECT_NAME} PRIVATE vsr_network)
endif()
if (TARGET vsr_scivis_studio_protocol)
  target_link_libraries(${PROJECT_NAME} PRIVATE vsr_scivis_studio_protocol)
endif()
if (TARGET vsr_scivis_studio_client_core)
  target_link_libraries(${PROJECT_NAME} PRIVATE vsr_scivis_studio_client_core)
endif()
if (TARGET vsr_scivis_studio_server_core)
  target_link_libraries(${PROJECT_NAME} PRIVATE vsr_scivis_studio_server_core)
endif()
if (TARGET vsr_scivis_studio_test_client_core)
  target_link_libraries(${PROJECT_NAME} PRIVATE vsr_scivis_studio_test_client_core)
endif()

add_test(NAME vsr::AnimationManager COMMAND ${PROJECT_NAME} "[AnimationManager]")
add_test(NAME vsr::App COMMAND ${PROJECT_NAME} "[App]")
add_test(NAME vsr::AnimationArchive COMMAND ${PROJECT_NAME} "[AnimationArchive]")
add_test(NAME vsr::AnimationManagerArchive
  COMMAND ${PROJECT_NAME} "[AnimationManagerArchive]")
add_test(NAME vsr::ArchiveCompatibility
  COMMAND ${PROJECT_NAME} "[ArchiveCompatibility]")
add_test(NAME vsr::CameraArchive COMMAND ${PROJECT_NAME} "[CameraArchive]")
add_test(NAME vsr::RendererArchive COMMAND ${PROJECT_NAME} "[RendererArchive]")
add_test(NAME vsr::Algorithms    COMMAND ${PROJECT_NAME} "[Algorithms]"   )
add_test(NAME vsr::AnyArray      COMMAND ${PROJECT_NAME} "[AnyArray]"     )
add_test(NAME vsr::Array         COMMAND ${PROJECT_NAME} "[Array]"        )
add_test(NAME vsr::ComponentSerialization
  COMMAND ${PROJECT_NAME} "[ComponentSerialization]")
add_test(NAME vsr::DataPath      COMMAND ${PROJECT_NAME} "[DataPath]"     )
add_test(NAME vsr::DataTree      COMMAND ${PROJECT_NAME} "[DataTree]"     )
add_test(NAME vsr::FlatMap       COMMAND ${PROJECT_NAME} "[FlatMap]"      )
add_test(NAME vsr::Forest        COMMAND ${PROJECT_NAME} "[Forest]"       )
add_test(NAME vsr::Geometry      COMMAND ${PROJECT_NAME} "[Geometry]"     )
add_test(NAME vsr::ImageImport   COMMAND ${PROJECT_NAME} "[ImageImport]"  )
add_test(NAME vsr::Importers     COMMAND ${PROJECT_NAME} "[Importers]"    )
add_test(NAME vsr::LayerSubtreeArchive
  COMMAND ${PROJECT_NAME} "[LayerSubtreeArchive]")
add_test(NAME vsr::Manipulator   COMMAND ${PROJECT_NAME} "[Manipulator]"  )
add_test(NAME vsr::ObjectPool    COMMAND ${PROJECT_NAME} "[ObjectPool]"   )
add_test(NAME vsr::Material      COMMAND ${PROJECT_NAME} "[Material]"     )
add_test(NAME vsr::Math          COMMAND ${PROJECT_NAME} "[Math]"         )
add_test(NAME vsr::Object        COMMAND ${PROJECT_NAME} "[Object]"       )
add_test(NAME vsr::ObjectArchive COMMAND ${PROJECT_NAME} "[ObjectArchive]")
add_test(NAME vsr::ObjectUsePtr  COMMAND ${PROJECT_NAME} "[ObjectUsePtr]" )
add_test(NAME vsr::Parameter     COMMAND ${PROJECT_NAME} "[Parameter]"    )
add_test(NAME vsr::RenderIndex   COMMAND ${PROJECT_NAME} "[RenderIndex]"  )
add_test(NAME vsr::Scene         COMMAND ${PROJECT_NAME} "[Scene]"        )
add_test(NAME vsr::SceneArchive  COMMAND ${PROJECT_NAME} "[SceneArchive]" )
add_test(NAME vsr::Token         COMMAND ${PROJECT_NAME} "[Token]"        )
add_test(NAME vsr::UsdImport     COMMAND ${PROJECT_NAME} "[UsdImport]"    )
if (TARGET vsr_scivis_studio_model)
  add_test(NAME vsr::SciVisStudio COMMAND ${PROJECT_NAME} "[SciVisStudio]")
endif()
if (TARGET vsr_network)
  add_test(NAME vsr::Network COMMAND ${PROJECT_NAME} "[Network]")
endif()
if (TARGET vsr_scivis_studio_protocol)
  add_test(NAME vsr::StudioProtocol COMMAND ${PROJECT_NAME} "[StudioProtocol]")
endif()
if (TARGET vsr_scivis_studio_client_core)
  add_test(NAME vsr::StudioClient COMMAND ${PROJECT_NAME} "[StudioClient]")
endif()
if (TARGET vsr_scivis_studio_server_core)
  add_test(NAME vsr::StudioServer COMMAND ${PROJECT_NAME} "[StudioServer]")
endif()
if (TARGET vsr_scivis_studio_client_core AND TARGET vsr_scivis_studio_server_core)
  add_test(NAME vsr::StudioRemote COMMAND ${PROJECT_NAME} "[StudioRemote]")
endif()
if (TARGET vsr_scivis_studio_test_client_core AND TARGET vsr_scivis_studio_server_core)
  add_test(NAME vsr::StudioTestClient COMMAND ${PROJECT_NAME} "[StudioTestClient]")
endif()
