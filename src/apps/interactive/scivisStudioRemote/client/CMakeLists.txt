## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# UI-agnostic client core: connection state machine, Structural Mirror
# delegate and Project Replica. No vsr_ui_imgui dependency so it stays unit
# testable; the scivisStudioClient executable builds on it below.
add_library(vsr_scivis_studio_client_core OBJECT
  MirrorUpdateDelegate.cpp
)

target_include_directories(vsr_scivis_studio_client_core
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(vsr_scivis_studio_client_core
PUBLIC
  vsr_scivis_studio_protocol
)
