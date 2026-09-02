// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ArrayHistogram.h"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <cmath>
#include <limits>

namespace vsr::scivis_studio::server {

namespace {

// Reads element i of `array` in the units ANARI samples it in.
template <int ANARI_ENUM_T>
float elementAsFloat(const vsr::scene::Array &array, size_t i)
{
  using properties_t = anari::ANARITypeProperties<ANARI_ENUM_T>;
  using base_t = typename properties_t::base_type;
  const auto *data = static_cast<const base_t *>(array.data());
  vsr::math::float4 out{0.f, 0.f, 0.f, 0.f};
  properties_t::toFloat4(&out.x, data + i);
  return out.x;
}

template <int ANARI_ENUM_T>
void histogram(const vsr::scene::Array &array,
    uint32_t binCount,
    protocol::ArrayHistogramResult &result)
{
  const size_t count = array.data() ? array.size() : 0;
  result.bins.assign(binCount, 0);
  result.minValue = 0.f;
  result.maxValue = 0.f;
  if (count == 0)
    return;

  float minValue = std::numeric_limits<float>::max();
  float maxValue = std::numeric_limits<float>::lowest();
  for (size_t i = 0; i < count; ++i) {
    const float v = elementAsFloat<ANARI_ENUM_T>(array, i);
    if (std::isnan(v))
      continue;
    minValue = std::min(minValue, v);
    maxValue = std::max(maxValue, v);
  }
  if (minValue > maxValue) { // every element was NaN
    result.bins[0] = count;
    return;
  }
  result.minValue = minValue;
  result.maxValue = maxValue;

  const float span = maxValue - minValue;
  const float scale = span > 0.f ? float(binCount) / span : 0.f;
  const size_t lastBin = binCount - 1;
  for (size_t i = 0; i < count; ++i) {
    const float v = elementAsFloat<ANARI_ENUM_T>(array, i);
    size_t bin = 0;
    if (!std::isnan(v) && scale > 0.f) {
      const float scaled = (v - minValue) * scale;
      bin = scaled <= 0.f ? 0 : std::min(size_t(scaled), lastBin);
    }
    ++result.bins[bin];
  }
}

} // namespace

bool computeArrayHistogram(const vsr::scene::Array &array,
    uint32_t binCount,
    protocol::ArrayHistogramResult &result,
    std::string *error)
{
  const auto fail = [&](const std::string &reason) {
    if (error)
      *error = reason;
    return false;
  };

  const auto type = array.elementType();
  if (array.isProxy())
    return fail("array holds no data on the server (proxy array)");
  if (array.isCUDA())
    return fail("histograms of CUDA arrays are not supported");
  if (anari::isObject(type) || anari::componentsOf(type) != 1) {
    return fail(std::string("array element type ") + anari::toString(type)
        + " is not scalar");
  }

  binCount = std::clamp(binCount, MIN_HISTOGRAM_BINS, MAX_HISTOGRAM_BINS);

  switch (type) {
  case ANARI_INT8:
    histogram<ANARI_INT8>(array, binCount, result);
    return true;
  case ANARI_UINT8:
    histogram<ANARI_UINT8>(array, binCount, result);
    return true;
  case ANARI_INT16:
    histogram<ANARI_INT16>(array, binCount, result);
    return true;
  case ANARI_UINT16:
    histogram<ANARI_UINT16>(array, binCount, result);
    return true;
  case ANARI_INT32:
    histogram<ANARI_INT32>(array, binCount, result);
    return true;
  case ANARI_UINT32:
    histogram<ANARI_UINT32>(array, binCount, result);
    return true;
  case ANARI_INT64:
    histogram<ANARI_INT64>(array, binCount, result);
    return true;
  case ANARI_UINT64:
    histogram<ANARI_UINT64>(array, binCount, result);
    return true;
  case ANARI_FIXED8:
    histogram<ANARI_FIXED8>(array, binCount, result);
    return true;
  case ANARI_UFIXED8:
    histogram<ANARI_UFIXED8>(array, binCount, result);
    return true;
  case ANARI_FIXED16:
    histogram<ANARI_FIXED16>(array, binCount, result);
    return true;
  case ANARI_UFIXED16:
    histogram<ANARI_UFIXED16>(array, binCount, result);
    return true;
  case ANARI_FIXED32:
    histogram<ANARI_FIXED32>(array, binCount, result);
    return true;
  case ANARI_UFIXED32:
    histogram<ANARI_UFIXED32>(array, binCount, result);
    return true;
  case ANARI_FIXED64:
    histogram<ANARI_FIXED64>(array, binCount, result);
    return true;
  case ANARI_UFIXED64:
    histogram<ANARI_UFIXED64>(array, binCount, result);
    return true;
  case ANARI_FLOAT32:
    histogram<ANARI_FLOAT32>(array, binCount, result);
    return true;
  case ANARI_FLOAT64:
    histogram<ANARI_FLOAT64>(array, binCount, result);
    return true;
  default:
    return fail(std::string("array element type ") + anari::toString(type)
        + " cannot be binned");
  }
}

} // namespace vsr::scivis_studio::server
