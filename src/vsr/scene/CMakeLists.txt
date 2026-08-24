## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsr_scene)

project_add_library(STATIC)

set(VSR_CORE_CUDA_SOURCES
  algorithms/detail/computeScalarRange_fixed16.cpp
  algorithms/detail/computeScalarRange_fixed8.cpp
  algorithms/detail/computeScalarRange_float32.cpp
  algorithms/detail/computeScalarRange_float64.cpp
  algorithms/detail/computeScalarRange_ufixed16.cpp
  algorithms/detail/computeScalarRange_ufixed8.cpp
)

set(VSR_CORE_CXX_SOURCES
  AnariHandleCache.cpp
  Layer.cpp
  LayerNodeData.cpp
  Parameter.cpp
  Object.cpp
  Scene.cpp
  UpdateDelegate.cpp
  algorithms/computeScalarRange.cpp
  objects/Array.cpp
  objects/Camera.cpp
  objects/Geometry.cpp
  objects/Light.cpp
  objects/Material.cpp
  objects/Renderer.cpp
  objects/Sampler.cpp
  objects/SpatialField.cpp
  objects/Surface.cpp
  objects/Volume.cpp
)

project_sources(PRIVATE ${VSR_CORE_CUDA_SOURCES} ${VSR_CORE_CXX_SOURCES})
project_link_libraries(PUBLIC vsr_core)
project_include_directories(
INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>
)

if (VSR_USE_CUDA)
  set_source_files_properties(${VSR_CORE_CUDA_SOURCES}
    PROPERTIES
      COMPILE_OPTIONS "--extended-lambda;--expt-relaxed-constexpr;-Xcudafe=--diag_suppress=20012;-Wno-deprecated-gpu-targets"
      LANGUAGE CUDA
  )
endif()

# Set MSVC /Zc:preprocessor (needed for VS generator).
if(MSVC)
  set_source_files_properties(${VSR_CORE_CXX_SOURCES}
    PROPERTIES COMPILE_OPTIONS "/Zc:preprocessor"
  )
endif()
