## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(obj2header)
project_add_executable(obj2header.cpp)
project_link_libraries(vsr_core vsr_io vela_ext_tinyobjloader)

project(vsrPrint)
project_add_executable(vsrPrint.cpp)
project_link_libraries(vsr_core)

project(vsrConvert)
project_add_executable(vsrConvert.cpp)
project_link_libraries(vsr_core)

project(vsrAssemble)
project_add_executable(vsrAssemble.cpp)
project_link_libraries(vsr_app vsr_scene vsr_io)

project(vsrRender)
project_add_executable(vsrRender.cpp RenderDeviceConfig.cpp RenderRuntime.cpp)
project_link_libraries(
  vsr_app
  vsr_scene
  vsr_io
  vsr_rendering
)
install(TARGETS vsrRender RUNTIME DESTINATION bin)

if(VSR_USE_MPI)
  project_link_libraries(vsr_mpi)
  project_compile_definitions(PRIVATE VSR_USE_MPI)
endif()

if(VSR_USE_USD)
  project(vsrUsdInfo)
  project_add_executable(vsrUsdInfo.cpp)
  project_link_libraries(vsr_core vsr_io)
endif()

project(vsrVolumeToNanoVDB)
project_add_executable(vsrVolumeToNanoVDB.cpp)
project_link_libraries(vsr_scene vsr_io)

if(VSR_USE_LUA)
  project(vsrLua)
  project_add_executable(vsrLua.cpp)
  project_link_libraries(
    vsr_lua
    vsr_scene
    vsr_io
    vsr_rendering
    vela_ext_sol2
  )
endif()
