## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# Exercise the public build/install interface, never a previous user install.
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --config "${CONFIG}"
    --target help
  RESULT_VARIABLE result OUTPUT_VARIABLE targets ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Cannot list build targets: ${error}")
endif()
if(targets MATCHES "vsrOffline")
  message(FATAL_ERROR "Retired renderer is still a build target")
endif()

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef suffix)
set(stage "${BUILD_DIR}/render-tools-install-${suffix}")
if(EXISTS "${stage}")
  message(FATAL_ERROR "Install test destination already exists: ${stage}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}"
    --prefix "${stage}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Clean install failed: ${output}\n${error}")
endif()
if(NOT EXISTS "${stage}/bin/vsrRender${EXECUTABLE_SUFFIX}")
  message(FATAL_ERROR "Clean install is missing vsrRender: ${stage}")
endif()
if(EXISTS "${stage}/bin/vsrOffline${EXECUTABLE_SUFFIX}")
  message(FATAL_ERROR "Clean install contains the retired renderer: ${stage}")
endif()
execute_process(
  COMMAND "${stage}/bin/vsrRender${EXECUTABLE_SUFFIX}" --help
  TIMEOUT 20 RESULT_VARIABLE result OUTPUT_VARIABLE help ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT help MATCHES "Usage: vsrRender")
  message(FATAL_ERROR "Installed renderer help failed: ${result}\n${error}")
endif()
message(STATUS "Clean install provides working vsrRender only: ${stage}")
file(REMOVE_RECURSE "${stage}")
