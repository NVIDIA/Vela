// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>
#include <optional>

namespace vsr::algorithms::cpu {

// Mean of log2(luminance) over an interleaved RGBA float host buffer. Same
// API as vsr::algorithms::cuda::meanLogLuminance; the CUDA path reduces every
// texel exactly through an SPD-style downsampler, while this host path strides
// to a fixed sample budget (a full-image scan is too slow per frame on the
// CPU). The strided mean tracks the exact mean to within ~0.02 stops.
//
// Luminance is clamped into [MIN_LUMINANCE, MAX_LUMINANCE] with NaN treated as
// black (math::clampedLuminance), so the result is always finite.
//
// Returns no value when the reduction cannot be performed at all — a null
// buffer, an empty image, or more than UINT32_MAX texels. Callers must not
// substitute a number for absence: 0.f is a *valid* mean (luminance 1.0).
std::optional<float> meanLogLuminance(
    const float *hdrColor, uint32_t width, uint32_t height);

} // namespace vsr::algorithms::cpu
