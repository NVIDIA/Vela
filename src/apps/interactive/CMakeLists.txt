## SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(dataTreeEditor)
add_subdirectory(demos)
add_subdirectory(multiDeviceViewer)
add_subdirectory(scivisStudio)
add_subdirectory(viewer)
if (VSR_USE_MPI)
  add_subdirectory(mpiViewer)
endif()
if (VSR_USE_NETWORKING)
  add_subdirectory(network)
  add_subdirectory(scivisStudioRemote)
endif()
