// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "ViewportMessages.h"
// vsr_scene
#include "vsr/scene/objects/Array.hpp"
// std
#include <cstdint>
#include <string>

namespace vsr::scivis_studio::server {

/*
 * The reduction behind RequestArrayHistogram: min, max and per-bin counts of
 * a scalar host Array, binCount clamped to [protocol::MIN_HISTOGRAM_BINS,
 * protocol::MAX_HISTOGRAM_BINS]. Fixed-point element types count in the
 * normalized value range ANARI samples them in (as
 * SpatialField::computeValueRange does); every other scalar type in its own
 * units. The last bin is closed, so the maximum lands in bins.back(); an
 * array whose finite values are all equal puts them all in bins[0] with
 * minValue == maxValue. NaN and infinite elements (FLOAT64 values beyond
 * float range included) are left out of the range and the bins and counted
 * in nonFinite; an array with no finite element reports an empty range
 * (0, 0) and empty bins.
 *
 * Refused, with the reason in `error`: proxy arrays (descriptors without
 * data), CUDA arrays and non-scalar element types (vectors, matrices, object
 * handles). Linear in the element count on the calling thread.
 *
 * Example:
 *   protocol::ArrayHistogramResult result;
 *   std::string error;
 *   if (!computeArrayHistogram(array, 64, result, &error))
 *     fail(error);
 */
bool computeArrayHistogram(const vsr::scene::Array &array,
    uint32_t binCount,
    protocol::ArrayHistogramResult &result,
    std::string *error = nullptr);

} // namespace vsr::scivis_studio::server
