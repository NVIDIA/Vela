## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsrDataTreeEditor)
project_add_executable(DataTreeWindow.cpp vsrDataTreeEditor.cpp)
project_link_libraries(vsr_ui_imgui)
