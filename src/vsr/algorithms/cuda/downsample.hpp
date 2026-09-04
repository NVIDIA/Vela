// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// cuda
#include <cuda_runtime_api.h>
// std
#include <cstddef>
#include <cstdint>
#include <optional>

namespace vsr::algorithms::cuda {

// Persistent device scratch for repeated meanLogLuminance calls: the mip
// chain plus the downsampler's tile counter, kept alive across frames so
// per-call cost is one counter memset and the reduction kernels — never a
// device alloc/free cycle (per-call alloc/free costs several times the
// reduction itself under per-frame stream syncs).
//
// Grow-only: reallocates at most once per larger image. A smaller image
// reuses the buffer with its own (smaller) layout, so window resizes are
// free. Not thread-safe and not multi-device — hold one per calling context
// (the AutoExposurePass owns one).
//
// Owns a raw device allocation, so it is neither copyable nor movable. The
// move operations are spelled out alongside the copy ones: deleting the copy
// operations already suppresses them, but stating it keeps a later reader
// from "restoring" a move that would double-free `storage`. Explicit
// `= delete` rather than the VSR_NOT_COPYABLE macros because this header
// deliberately carries no vsr_core dependency (STYLEGUIDE Sec. 6).
struct MeanLogLuminanceScratch
{
  MeanLogLuminanceScratch() = default;
  ~MeanLogLuminanceScratch();

  MeanLogLuminanceScratch(const MeanLogLuminanceScratch &) = delete;
  MeanLogLuminanceScratch &operator=(const MeanLogLuminanceScratch &) = delete;
  MeanLogLuminanceScratch(MeanLogLuminanceScratch &&) = delete;
  MeanLogLuminanceScratch &operator=(MeanLogLuminanceScratch &&) = delete;

  float *storage{nullptr};  // [mip levels..., tile counter]
  size_t capacityTexels{0}; // floats available at `storage` (0 = empty)
};

// Exact full-image mean of log2(luminance) over an interleaved RGBA32F
// device buffer, reduced through an SPD-style single-pass downsampler
// (detail/SinglePassDownsampler.h) with identity (zero) padding, so the sum
// counts every texel exactly once regardless of dimensions. `scratch` is
// grown as needed and reused across calls. Synchronizes `stream` to return
// the value.
//
// Luminance is clamped into [MIN_LUMINANCE, MAX_LUMINANCE] with NaN treated
// as black (math::clampedLuminance), matching the CPU backend texel for
// texel, so the result is always finite.
//
// Returns no value when the reduction cannot be performed or a CUDA call
// fails (bad arguments, failed scratch allocation, launch or copy error).
// Callers must not substitute a number for absence: 0.f is a *valid* mean
// (luminance 1.0). A failure leaves the previous scratch capacity intact.
std::optional<float> meanLogLuminance(MeanLogLuminanceScratch &scratch,
    cudaStream_t stream,
    const float *hdrColor,
    uint32_t width,
    uint32_t height);

} // namespace vsr::algorithms::cuda
