# SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

project(vela_ext_stb_image LANGUAGES C)
project_add_library(STATIC stb_image_write.c stb_image.c)
project_include_directories(PUBLIC ${CMAKE_CURRENT_LIST_DIR})
add_library(stb_image ALIAS vela_ext_stb_image)