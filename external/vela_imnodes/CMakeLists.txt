## SPDX-FileCopyrightText: Copyright (c) 2023-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vela_ext_imnodes LANGUAGES CXX)

anari_sdk_fetch_project(
  NAME ${PROJECT_NAME}
  URL https://github.com/Nelarius/imnodes/archive/refs/tags/v0.5.zip
  MD5 bebc93614fb55fc5924191b3c5320026
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

