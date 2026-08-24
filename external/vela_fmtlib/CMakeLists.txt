# SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if (TARGET fmt)
  return()
endif()

add_library(fmt STATIC src/format.cc)
target_include_directories(fmt PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_compile_definitions(fmt PUBLIC FMT_UNICODE=0)
add_library(fmt::fmt ALIAS fmt)
add_library(vela::fmt ALIAS fmt)

