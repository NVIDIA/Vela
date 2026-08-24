// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>

namespace vsr::core {

using ObjectVersion = uint64_t;

bool versionChanged(ObjectVersion &lastChecked, const ObjectVersion &current);

// Inlined definitions ////////////////////////////////////////////////////////

inline bool versionChanged(
    ObjectVersion &lastChecked, const ObjectVersion &current)
{
  if (lastChecked < current) {
    lastChecked = current;
    return true;
  }
  return false;
}

} // namespace vsr::core
