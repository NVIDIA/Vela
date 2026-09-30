// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <cstdint>
#include <optional>

namespace vsr::algorithms::cpu {

// Mean of log2(luminance) over an interleaved RGBA float host buffer. The
// CUDA path (vsr::algorithms::cuda::meanLogLuminance) reduces every texel
// exactly; this host path estimates the mean from a fixed budget of
// stratified samples — one jittered texel per square cell, weighted by cell
// area — so its cost does not grow with resolution. Images within the budget
// are reduced exactly, matching the CUDA result.
//
// `seed` picks the jitter. Vary it per frame (e.g. a frame counter) so the
// sampling error averages out under temporal smoothing instead of freezing
// into a constant bias; the same seed always yields the same result.
//
// Luminance is clamped into [MIN_LUMINANCE, MAX_LUMINANCE] with NaN treated as
// black (math::clampedLuminance), so the result is always finite.
//
// Returns no value when the reduction cannot be performed at all — a null
// buffer, an empty image, or more than UINT32_MAX texels. Callers must not
// substitute a number for absence: 0.f is a *valid* mean (luminance 1.0).
std::optional<float> meanLogLuminance(
    const float *hdrColor, uint32_t width, uint32_t height, uint32_t seed);

} // namespace vsr::algorithms::cpu
