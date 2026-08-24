## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# Always present components
add_subdirectory(app)
add_subdirectory(core)
add_subdirectory(algorithms)
add_subdirectory(io)
add_subdirectory(animation)
add_subdirectory(rendering)
add_subdirectory(scene)

# Optional components
if (VSR_USE_MPI)
  add_subdirectory(mpi)
endif()
if (VSR_USE_NETWORKING)
  add_subdirectory(network)
endif()
if (VSR_BUILD_UI_LIBRARY)
  add_subdirectory(ui)
endif()
if (VSR_USE_LUA)
  add_subdirectory(scripting)
endif()
