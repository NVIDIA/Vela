// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef __CUDACC__
#define VSR_DEVICE_FCN __device__
#define VSR_DEVICE_FCN_INLINE __forceinline__ __device__
#define VSR_HOST_DEVICE_FCN __host__ __device__
#else
#define VSR_DEVICE_FCN
#define VSR_DEVICE_FCN_INLINE inline
#define VSR_HOST_DEVICE_FCN
#endif
