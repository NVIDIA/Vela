// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace vsr::algorithms::cpu {

void fill(uint32_t *buf, uint32_t count, uint32_t value);
void fill(float *buf, uint32_t count, float value);

} // namespace vsr::algorithms::cpu
