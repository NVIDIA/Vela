// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "computeScalarRangeImpl.hpp"

namespace vsr::scene::detail {

vsr::math::float2 computeScalarRange_float64(const Array &a)
{
  return computeScalarRangeImpl<ANARI_FLOAT64>(a);
}

} // namespace vsr::scene::detail
