// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/algorithms/cpu/clearBuffers.hpp"
// std
#include <algorithm>

namespace vsr::algorithms::cpu {

void fill(uint32_t *buf, uint32_t count, uint32_t value)
{
  std::fill(buf, buf + count, value);
}

void fill(float *buf, uint32_t count, float value)
{
  std::fill(buf, buf + count, value);
}

} // namespace vsr::algorithms::cpu
