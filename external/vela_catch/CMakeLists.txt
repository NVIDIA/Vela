## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

if (BUILD_TESTING)
  project(vela_ext_catch2)
  project_add_library(STATIC catch_main.cpp)
  project_include_directories(PUBLIC ${CMAKE_CURRENT_LIST_DIR}/catch2)
endif()