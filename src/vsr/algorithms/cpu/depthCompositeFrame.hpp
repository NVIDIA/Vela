// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>

namespace vsr::algorithms::cpu {

void depthCompositeFrame(uint32_t *outColor,
    float *outDepth,
    uint32_t *outObjectId,
    const uint32_t *inColor,
    const float *inDepth,
    const uint32_t *inObjectId,
    uint32_t pixelCount,
    bool firstPass);

} // namespace vsr::algorithms::cpu
