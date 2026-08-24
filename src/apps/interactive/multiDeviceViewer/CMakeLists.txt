## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsrMultiDeviceViewer)
project_add_executable(multiDeviceViewer.cpp)
project_link_libraries(PUBLIC vsr_ui_imgui)
