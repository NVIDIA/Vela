## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# The headless test client: a second, independent client implementation
# driven by a script. Everything but main() lives in the core so vsrTests can
# run scripts against an in-process StudioServer. No SDL, no ImGui, and
# nothing from client/: the protocol library is all it shares with the GUI.
add_library(vsr_scivis_studio_test_client_core OBJECT
  CommandRunner.cpp
  Script.cpp
  TestClientOptions.cpp
  TestSession.cpp
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

add_executable(scivisStudioTestClient scivisStudioTestClient.cpp)

# OBJECT libraries do not hand their object files on transitively, so the
# executable names every one it depends on.
target_link_libraries(scivisStudioTestClient
PRIVATE
  vsr_scivis_studio_test_client_core
  vsr_scivis_studio_protocol
  vsr_scivis_studio_model
)

## Scenario tests ##

# One ctest per scenario script, run through run_scenario.sh against a server
# started on a free port. Exit 77 from the runner (no ANARI device loads) is a
# SKIP, not a failure. The turbojpeg scenario needs both ends to encode and
# decode JPEG, so it exists only when the tree has it.
if (NOT BUILD_TESTING)
  return()
endif()

set(scenario_dir ${CMAKE_CURRENT_LIST_DIR}/scenarios)

function(add_studio_scenario name)
  add_test(NAME vsr::StudioScenario.${name}
    COMMAND ${scenario_dir}/run_scenario.sh
      $<TARGET_FILE:scivisStudioServer>
      $<TARGET_FILE:scivisStudioTestClient>
      ${scenario_dir}/${name}.studio
  )
  set_tests_properties(vsr::StudioScenario.${name} PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 120
  )
endfunction()

# The executable itself: a script with nothing to run exits 2 with a message
# instead of passing by running nothing.
add_test(NAME vsr::StudioTestClient.emptyScript
  COMMAND scivisStudioTestClient -e "# nothing to run"
)
set_tests_properties(vsr::StudioTestClient.emptyScript PROPERTIES
  PASS_REGULAR_EXPRESSION "no commands to run"
)

add_studio_scenario(session)
add_studio_scenario(frames_raw)
if (VSR_USE_TURBOJPEG)
  add_studio_scenario(frames_turbojpeg)
endif()
add_studio_scenario(scene_edits)
add_studio_scenario(errors)
# loss.studio carries the `# runner: kill-restart-after` hint, so the same
# runner kills and restarts the server mid-script.
add_studio_scenario(loss)
