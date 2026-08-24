// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifdef ENABLE_TBB
#include <tbb/blocked_range.h>
#include <tbb/parallel_reduce.h>
#endif

#include <cstdint>

namespace vsr::algorithms::cpu::detail {

template <typename T, typename TRANSFORM_FCN, typename REDUCE_FCN>
inline T parallel_reduce(uint32_t start,
    uint32_t end,
    T init,
    TRANSFORM_FCN &&transform,
    REDUCE_FCN &&reduce)
{
#ifdef ENABLE_TBB
  return tbb::parallel_reduce(
      tbb::blocked_range<uint32_t>(start, end),
      init,
      [&](const tbb::blocked_range<uint32_t> &r, T partial) {
        for (auto i = r.begin(); i < r.end(); i++)
          partial = reduce(partial, transform(i));
        return partial;
      },
      reduce);
#else
  T result = init;
  for (auto i = start; i < end; i++)
    result = reduce(result, transform(i));
  return result;
#endif
}

} // namespace vsr::algorithms::cpu::detail
