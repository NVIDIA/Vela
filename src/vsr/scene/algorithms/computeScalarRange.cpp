// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/scene/algorithms/computeScalarRange.hpp"
#include "vsr/scene/Scene.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"

#include "vsr/scene/algorithms/detail/computeScalarRangeImpl.hpp"

namespace vsr::scene {

vsr::math::float2 computeScalarRange(const Array &a)
{
  constexpr float maxFloat = (std::numeric_limits<float>::max)();
  vsr::math::float2 retval{maxFloat, -maxFloat};

  const anari::DataType type = a.elementType();
  const bool elementsAreArrays = anari::isArray(type);
  const bool elementsAreScalars =
      !anari::isObject(type) && anari::componentsOf(type) == 1;

  if (auto *scene = a.scene(); elementsAreArrays && scene) {
    const auto *begin = (uint64_t *)a.data();
    const auto *end = begin + a.size();
    std::for_each(begin, end, [&](uint64_t idx) {
      vsr::math::float2 subRange{maxFloat, -maxFloat};
      if (auto subArray = scene->template getObject<Array>(idx); subArray)
        subRange = computeScalarRange(*subArray);
      retval.x = (std::min)(retval.x, subRange.x);
      retval.y = (std::max)(retval.y, subRange.y);
    });
  } else if (elementsAreScalars) {
    switch (type) {
    case ANARI_UFIXED8:
      retval = detail::computeScalarRange_ufixed8(a);
      break;
    case ANARI_UFIXED16:
      retval = detail::computeScalarRange_ufixed16(a);
      break;
    case ANARI_FIXED8:
      retval = detail::computeScalarRange_fixed8(a);
      break;
    case ANARI_FIXED16:
      retval = detail::computeScalarRange_fixed16(a);
      break;
    case ANARI_FLOAT32:
      retval = detail::computeScalarRange_float32(a);
      break;
    case ANARI_FLOAT64:
      retval = detail::computeScalarRange_float64(a);
      break;
    default:
      logWarning(
          "computeScalarRange() called on an "
          "array with incompatible element type '%s'",
          anari::toString(type));
      break;
    }
  }

  return retval;
}

} // namespace vsr::scene
