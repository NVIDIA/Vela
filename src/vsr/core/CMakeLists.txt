## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsr_core)

project_add_library(STATIC)

if (VSR_USE_CUDA)
  list(APPEND VSR_CORE_CUDA_SOURCES algorithms/detail/vort_cuda.cu)
endif()

set(VSR_CORE_CXX_SOURCES
  ColorMapUtil.cpp
  DataStream.cpp
  Logging.cpp
  Token.cpp
)

project_sources(PRIVATE ${VSR_CORE_CUDA_SOURCES} ${VSR_CORE_CXX_SOURCES})

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>
)

project_link_libraries(PUBLIC anari::helium fmt::fmt)

if (WIN32)
  project_link_libraries(PUBLIC anari::anari_static)
else()
  project_link_libraries(PUBLIC anari::anari)
endif()

if (VSR_USE_CUDA)
  project_compile_definitions(PUBLIC -DVSR_USE_CUDA=1)
  project_link_libraries(PUBLIC CUDA::cudart)
  if(TARGET CCCL::CCCL)
    project_link_libraries(PUBLIC CCCL::CCCL)
  endif()
  set_source_files_properties(${VSR_CORE_CUDA_SOURCES}
    PROPERTIES
      COMPILE_OPTIONS "--extended-lambda;--expt-relaxed-constexpr;-Xcudafe=--diag_suppress=20012;-Wno-deprecated-gpu-targets"
      LANGUAGE CUDA
  )
else()
  project_compile_definitions(PUBLIC -DVSR_USE_CUDA=0)
endif()

# Set MSVC /Zc:preprocessor (needed for VS generator).
if(MSVC)
  set_source_files_properties(${VSR_CORE_CXX_SOURCES}
    PROPERTIES COMPILE_OPTIONS "/Zc:preprocessor"
  )
endif()
