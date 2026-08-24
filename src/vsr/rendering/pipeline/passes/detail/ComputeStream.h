// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef ENABLE_CUDA
// cuda
#include <cuda_runtime_api.h>
#endif

namespace vsr::rendering::detail {

#ifdef ENABLE_CUDA
using ComputeStream = cudaStream_t;
#else
using ComputeStream = void *;
#endif

} // namespace vsr::rendering::detail
