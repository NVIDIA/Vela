## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

project(vsrMPIServer)
project_add_executable(DistributedRenderServer.cpp mpiServer.cpp)
project_link_libraries(vsr_app vsr_network vsr_mpi)
