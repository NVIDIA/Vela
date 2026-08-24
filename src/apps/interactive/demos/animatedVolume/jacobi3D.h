// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vsr::demo {

// Use existing GPU grids
void jacobi3D(
    int nx, int ny, int nz, float *d_grid, float *d_old_grid, int iterations);

// Use host grid
void jacobi3D(int nx, int ny, int nz, float *h_grid, int iterations);

} // namespace vsr::demo
