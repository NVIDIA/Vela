## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(animatedParticles)
add_subdirectory(animatedVolume)
add_subdirectory(arrayInstancing)
add_subdirectory(manualAccumulationReset)
add_subdirectory(customField)
option(BUILD_VISKORES_DEMO "Build Viskores demo viewer" OFF)
if (BUILD_VISKORES_DEMO)
  add_subdirectory(viskores)
endif()
