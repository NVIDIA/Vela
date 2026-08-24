## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_tinyexr)
project_add_library(INTERFACE)
project_include_directories(INTERFACE ${CMAKE_CURRENT_LIST_DIR})

project(vela_ext_tinyexr_impl)
project_add_library(INTERFACE)
project_sources(INTERFACE tinyexr.cpp)
