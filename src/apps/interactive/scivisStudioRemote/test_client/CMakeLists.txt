## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# The headless test client: a second, independent client implementation
# driven by a script. Everything but main() lives in the core so vsrTests can
# run scripts against an in-process StudioServer. No SDL, no ImGui, and
# nothing from client/: the protocol library is all it shares with the GUI.
add_library(vsr_scivis_studio_test_client_core OBJECT
  Script.cpp
)

target_include_directories(vsr_scivis_studio_test_client_core
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(vsr_scivis_studio_test_client_core
PUBLIC
  vsr_scivis_studio_protocol
  vsr_scivis_studio_model
  vsr_network
  vsr_scene
)
