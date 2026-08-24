// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// usd
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/dataSourceTypeDefs.h>

namespace vsr::io::usd {

// Read an int-array data source, or an empty array when it is absent. Every
// Hydra schema hands topology out this way, and every reader of one wants the
// same "absent is empty" answer.
inline pxr::VtIntArray intArrayOf(const pxr::HdIntArrayDataSourceHandle &source)
{
  return source ? source->GetTypedValue(0) : pxr::VtIntArray();
}

} // namespace vsr::io::usd
