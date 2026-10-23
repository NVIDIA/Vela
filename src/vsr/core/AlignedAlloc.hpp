// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstddef>
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace vsr::core {

// Allocates at least 'bytes' bytes aligned to 'alignment' (a power of two),
// rounding the size up to a multiple of 'alignment' as std::aligned_alloc()
// requires. Release with alignedFree(), never std::free() directly: MSVC has
// no std::aligned_alloc(), so Windows uses _aligned_malloc()/_aligned_free().
void *alignedAlloc(size_t alignment, size_t bytes);
void alignedFree(void *ptr);

// Inlined definitions ////////////////////////////////////////////////////////

inline void *alignedAlloc(size_t alignment, size_t bytes)
{
  const size_t rounded = (bytes + alignment - 1) / alignment * alignment;
#ifdef _WIN32
  return _aligned_malloc(rounded, alignment);
#else
  return std::aligned_alloc(alignment, rounded);
#endif
}

inline void alignedFree(void *ptr)
{
#ifdef _WIN32
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif
}

} // namespace vsr::core
