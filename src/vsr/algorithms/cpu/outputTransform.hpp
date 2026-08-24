// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anari/anari_cpp.hpp>
#include <cstdint>

namespace vsr::algorithms::cpu {

// Apply gamma correction and write packed uint32 RGBA output.
// Reads from hdrColor (float4 interleaved) or colorIn (packed uint32)
// depending on colorFormat. Writes to colorOut (packed uint32).
void outputTransform(const float *hdrColor,
    const uint32_t *colorIn,
    uint32_t *colorOut,
    uint32_t numPixels,
    float invGamma,
    anari::DataType colorFormat);

} // namespace vsr::algorithms::cpu
