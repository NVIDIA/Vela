## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(anari_vsr)
add_subdirectory(vsr)

# NOTE: apps must be added before tests -- tests/CMakeLists.txt gates the
# SciVis Studio suites on `if (TARGET vsr_scivis_studio_model)`, and that
# target is defined under apps/interactive/scivisStudio.
if (VSR_BUILD_APPS)
  add_subdirectory(apps)
endif()
add_subdirectory(tests)
