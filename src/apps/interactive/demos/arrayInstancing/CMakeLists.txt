## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsrDemoArrayInstancing)
project_add_executable(InstancingControls.cpp vsrDemoArrayInstancing.cpp)
project_link_libraries(vsr_ui_imgui)
