## SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_imnodes LANGUAGES CXX)

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/Nelarius/imnodes/archive/eb36902c892548ef94f88f51ad7e7c9c7058a71c.zip
  MD5 65292c755ccba3b5c3686262637d8ef8
)

project_add_library(INTERFACE)

project_sources(
INTERFACE
  $<BUILD_INTERFACE:${vela_ext_imnodes_LOCATION}/imnodes.cpp>
)

project_include_directories(
INTERFACE
  $<BUILD_INTERFACE:${vela_ext_imnodes_LOCATION}>
)

