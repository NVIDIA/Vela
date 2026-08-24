## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsr_device LANGUAGES CXX)

project_add_library(OBJECT
  Array.cpp
  Device.cpp
  DeviceGlobalState.cpp
  Frame.cpp
  Group.cpp
  Instance.cpp
  Object.cpp
  World.cpp
)
project_link_libraries(PUBLIC vsr_app vsr_io anari::helium)
project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

anari_generate_queries(
  DEVICE_TARGET ${PROJECT_NAME}
  CPP_NAMESPACE vsr_device
  JSON_DEFINITIONS_FILE ${ANARI_CODE_GEN_ROOT}/devices/experimental_device.json
)

## Installable VSR device library ##

project(anari_library_vsr LANGUAGES CXX)
project_add_library(SHARED Library.cpp)
project_link_libraries(PUBLIC vsr_device)

include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} EXPORT_MACRO_NAME "ANARI_VSR_EXPORT")

install(TARGETS ${PROJECT_NAME}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
