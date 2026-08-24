## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
  message(STATUS "CMake version: ${CMAKE_VERSION}")
  set(ANARI_REQUIRED_VERSION 0.15.0)
  set(VSR_STANDALONE_BUILD 1)
else()
  set(VSR_STANDALONE_BUILD 0)
endif()

## CMake setup ##

set(CMAKE_DISABLE_SOURCE_CHANGES ON)
set(CMAKE_DISABLE_IN_SOURCE_BUILD ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_TYPE_INIT Release)
set(CMAKE_INSTALL_MESSAGE LAZY)

set(CMAKE_CUDA_ARCHITECTURES all-major)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

include(CMakeDependentOption)

## Global options ##

option(VSR_BUILD_APPS "Build VSR applications" ON)
cmake_dependent_option(VSR_BUILD_INTERACTIVE_APPS
  "Build interactive applications"
  ON
  VSR_BUILD_APPS
  OFF
)
cmake_dependent_option(VSR_BUILD_UI_LIBRARY
  "Build the VSR ImGui UI library"
  ON
  "NOT VSR_BUILD_INTERACTIVE_APPS"
  ON
)

option(VSR_USE_HDF5 "Support loading AMR grids from HDF5" OFF)
option(VSR_USE_ASSIMP "Support loading geometry with libassimp" OFF)
option(VSR_USE_SDL3 "Use SDL3 interop in render pipeline" ON)
option(VSR_USE_TBB "Use TBB code paths where relevant" ON)
option(VSR_USE_USD "Use OpenUSD where relevant" OFF)
option(VSR_USE_TORCH "Use PyTorch for importing neural geometries" OFF)
option(VSR_USE_VTK "Use VTK for importing VTK file formats" OFF)
option(VSR_USE_SILO "Use Silo for importing Silo file formats" OFF)
option(VSR_USE_OIIO
  "Use OpenImageIO to decode texture formats stb cannot (e.g. TIFF)" OFF)
option(VSR_USE_MPI "Enable MPI support" OFF)
option(VSR_USE_NETWORKING "Enable networking support via boost.asio" OFF)
option(VSR_NANOVDB_SKIP_INVALID_VOLUMES
  "Skip NanoVDB volumes with zero active voxels" OFF)
option(VSR_NANOVDB_USE_ZIP
  "Enable zlib so NanoVDB importer can read ZIP-compressed .nvdb files" ON)
option(VSR_USE_LUA "Enable Lua scripting support" ON)

if (APPLE)
  set(VSR_USE_CUDA OFF)
else()
  option(VSR_USE_CUDA "Use CUDA code paths where relevant" ON)
endif()

## Establish project ##

project(vela LANGUAGES C CXX)

## Linker selection ##

if (UNIX AND NOT APPLE)
  foreach(candidate mold lld gold)
    find_program(VSR_LD_${candidate} ld.${candidate})
    if (VSR_LD_${candidate})
      add_link_options(-fuse-ld=${candidate})
      message(STATUS "Using ld.${candidate} for faster linking")
      break()
    endif()
  endforeach()
endif()

## Major dependencies ##

find_package(anari ${ANARI_REQUIRED_VERSION} REQUIRED)

if (VSR_USE_CUDA)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  if(CUDAToolkit_VERSION VERSION_GREATER_EQUAL "13.0")
    find_package(CCCL CONFIG REQUIRED HINTS ${CUDAToolkit_LIBRARY_DIR}/cmake)
  endif()
endif()

## Testing Setup ##

include(CTest)
if (BUILD_TESTING)
  enable_testing()
endif()

## Build Libraries ##

add_subdirectory(external)
add_subdirectory(src)

## Cleanup CMake variables ##

include(cmake/mark_cache_variables_as_advanced.cmake)
