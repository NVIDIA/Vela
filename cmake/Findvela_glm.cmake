# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if (TARGET vela::glm)
  return()
endif()

# glm
find_package(glm CONFIG QUIET)
if (TARGET glm::glm)
  message(STATUS "Found glm: (external) ${glm_DIR}")
else()
  # Use locally provided version of glm
  set(glm_DIR ${CMAKE_CURRENT_LIST_DIR}/../external/vela_glm/lib/cmake/glm)
  find_package(glm CONFIG REQUIRED)
  message(STATUS "Found glm: (internal) ${glm_DIR}")
endif()
mark_as_advanced(glm_DIR)

add_library(vela::glm INTERFACE IMPORTED)
target_link_libraries(vela::glm INTERFACE glm::glm)
target_compile_definitions(vela::glm INTERFACE GLM_ENABLE_EXPERIMENTAL)
if(WIN32)
  target_compile_definitions(vela::glm INTERFACE _USE_MATH_DEFINES NOMINMAX)
endif()
