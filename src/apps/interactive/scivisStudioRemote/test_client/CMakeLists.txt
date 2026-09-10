## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# The headless test client: the GUI client's connection core driven by a
# script instead of a UI. Everything but main() lives in this core so vsrTests
# can run scripts against an in-process StudioServer. No SDL and no ImGui: the
# GUI's own core is UI-free, so the session code is shared with it rather than
# written twice.
add_library(vsr_scivis_studio_test_client_core OBJECT
  AnyText.cpp
  CommandRunner.cpp
  CommandText.cpp
  NamedValues.cpp
  RecordFields.cpp
  RequestCommands.cpp
  SceneCommands.cpp
  Script.cpp
  ServerProcess.cpp
  SessionCommands.cpp
  TestClientOptions.cpp
  TestSession.cpp
  WaitCommands.cpp
)

target_include_directories(vsr_scivis_studio_test_client_core
PUBLIC
  ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(vsr_scivis_studio_test_client_core
PUBLIC
  vsr_scivis_studio_client_core
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
  vsr_scivis_studio_client_core
  vsr_scivis_studio_protocol
  vsr_scivis_studio_model
)

## Scenario tests ##

# One ctest per scenario script: the test client spawns a server on a port of
# the OS's choosing in a fresh temporary directory (--spawn-server), runs the
# script against it and stops it. Exit 77 (--require-device: no ANARI device
# loads) is a SKIP, not a failure. The turbojpeg scenario needs both ends to
# encode and decode JPEG, so it exists only when the tree has it.
if (NOT BUILD_TESTING)
  return()
endif()

set(scenario_dir ${CMAKE_CURRENT_LIST_DIR}/scenarios)

function(add_studio_scenario name)
  add_test(NAME vsr::StudioScenario.${name}
    COMMAND scivisStudioTestClient
      --script ${scenario_dir}/${name}.studio
      --require-device
      --spawn-server $<TARGET_FILE:scivisStudioServer> --library helide
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
# loss.studio kills and restarts the spawned server mid-script (kill-server,
# await-server).
add_studio_scenario(loss)
# Milestone 5: Project Ops, Server Tasks and Remote Browse. datasets and tasks
# copy-fixture a tiny OBJ into the data root.
add_studio_scenario(project_lifecycle)
add_studio_scenario(rigs)
add_studio_scenario(color_maps)
add_studio_scenario(datasets)
add_studio_scenario(mirror_parameters)
add_studio_scenario(tasks)
add_studio_scenario(browse)
add_studio_scenario(errors_project)
# The whole milestone-5 surface in one session, in orders the per-area
# scenarios do not take (save, new, reopen; sync ops queued behind tasks).
add_studio_scenario(all_m5)
# Milestone 6: playback, picking, viewport passes and the array histogram.
# pick and histogram import the fixture OBJ.
add_studio_scenario(playback)
add_studio_scenario(autostop)
add_studio_scenario(scrub)
add_studio_scenario(pick)
add_studio_scenario(viewport)
add_studio_scenario(histogram)
# Milestone 7: offline shot rendering, its cancel and pause-and-refuse, the
# task-status replay, and loss while a task runs (kill-server mid-render).
# Each imports the fixture OBJ.
add_studio_scenario(render_shot)
add_studio_scenario(render_cancel)
add_studio_scenario(task_replay)
add_studio_scenario(loss_during_task)
