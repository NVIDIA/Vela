## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_nanovdb LANGUAGES CXX)
project_add_library(INTERFACE)
project_include_directories(INTERFACE ${CMAKE_CURRENT_LIST_DIR})
if (VSR_USE_TBB)
  find_package(TBB REQUIRED)
  project_link_libraries(INTERFACE TBB::tbb)
  project_compile_definitions(INTERFACE NANOVDB_USE_TBB)
endif()

if (VSR_NANOVDB_USE_ZIP)
  # zlib decompresses ZIP-encoded .nvdb files at read time. ANARI always
  # receives the decompressed in-memory grid buffer — the codec only
  # affects on-disk storage. The vsr FindZLIB.cmake module supplies a
  # fetched static fallback when no system zlib is available.
  project_link_libraries(INTERFACE vela_ext_zlib)
  project_compile_definitions(INTERFACE NANOVDB_USE_ZIP)
endif()
