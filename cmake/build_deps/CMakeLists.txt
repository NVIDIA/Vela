# SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.12)

if(NOT CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX
      "${CMAKE_BINARY_DIR}/install"
      CACHE STRING "Final install location." FORCE)
endif()

project(vsr_dependencies)

include(superbuild_macros.cmake)

## Build projects ##

build_subproject(
  NAME anari-sdk
  URL "https://github.com/KhronosGroup/ANARI-SDK/archive/refs/heads/next_release.zip"
  BUILD_ARGS
    -DBUILD_EXAMPLES=OFF
    -DBUILD_HDANARI=OFF
    -DBUILD_HELIDE_DEVICE=OFF
    -DBUILD_REMOTE_DEVICE=OFF
    -DBUILD_TESTING=OFF
)
