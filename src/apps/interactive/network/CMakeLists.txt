## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

add_subdirectory(client)
add_subdirectory(server)
if (VSR_USE_MPI)
  add_subdirectory(mpiServer)
endif()
