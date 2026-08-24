// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace vsr::algorithms::cpu {

void convertFloatToUint8(const float *src, uint8_t *dst, size_t count);

} // namespace vsr::algorithms::cpu
