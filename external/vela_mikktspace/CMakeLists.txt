## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_mikktspace LANGUAGES C)
project_add_library(STATIC mikktspace.c)
project_include_directories(PUBLIC ${CMAKE_CURRENT_LIST_DIR})
