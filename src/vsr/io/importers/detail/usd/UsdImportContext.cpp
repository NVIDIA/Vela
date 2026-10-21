// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/importers/detail/usd/UsdImportContext.h"
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/io/importers/detail/usd/UsdDialect.h"
// usd
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usdGeom/pointBased.h>
// std
#include <algorithm>
#include <cstring>
#include <functional>
#include <string_view>
#include <vector>

namespace vsr::io::usd {

vsr::animation::Animation &ImportContext::animation()
{
  if (importAnimationIndex == NO_ANIMATION) {
    animMgr->addAnimation(filePath);
    importAnimationIndex = animMgr->animations().size() - 1;
  }
  return animMgr->animations()[importAnimationIndex];
}

void ImportContext::reportAnimatedPrim(size_t sampleCount)
{
  report->animatedPrims++;
  report->sampleCount = std::max(report->sampleCount, sampleCount);
  if (session)
    report->timeCodesPerSecond = float(session->timeCodesPerSecond());
}

namespace {

size_t contentHash(anari::DataType type, size_t count, const void *data)
{
  const auto numBytes = count * anari::sizeOf(type);
  const auto bytes = std::hash<std::string_view>{}(
      std::string_view(static_cast<const char *>(data), numBytes));
  return bytes ^ (size_t(type) * 0x9e3779b97f4a7c15ull) ^ count;
}

} // namespace

ArrayRef ArrayContentCache::find(
    anari::DataType type, size_t count, const void *data) const
{
  auto found = m_arraysByHash.find(contentHash(type, count, data));
  if (found == m_arraysByHash.end())
    return {};

  const auto numBytes = count * anari::sizeOf(type);
  for (const auto &array : found->second) {
    if (array->elementType() == type && array->size() == count
        && std::memcmp(array->data(), data, numBytes) == 0)
      return array;
  }
  return {};
}

void ArrayContentCache::insert(ArrayRef array)
{
  const auto hash =
      contentHash(array->elementType(), array->size(), array->data());
  m_arraysByHash[hash].push_back(array);
}

bool ImportContext::isClaimed(const pxr::SdfPath &path) const
{
  return claimedPrims && claimedPrims->claims(path);
}

bool attributeValueVaries(const pxr::UsdAttribute &attribute)
{
  if (!attribute)
    return false;

  std::vector<double> times;
  attribute.GetTimeSamples(&times);
  if (times.size() < 2)
    return false;

  // Reading every sample of an array attribute means reading the whole file.
  if (attribute.GetTypeName().IsArray())
    return true;

  pxr::VtValue first;
  if (!attribute.Get(&first, pxr::UsdTimeCode(times.front())))
    return false;

  for (size_t i = 1; i < times.size(); ++i) {
    pxr::VtValue value;
    if (!attribute.Get(&value, pxr::UsdTimeCode(times[i])))
      return true;
    if (value != first)
      return true;
  }
  return false;
}

bool geometryDeforms(const pxr::UsdPrim &prim)
{
  pxr::UsdGeomPointBased pointBased(prim);
  if (!pointBased)
    return false;
  std::vector<double> sampleTimes;
  pointBased.GetPointsAttr().GetTimeSamples(&sampleTimes);
  return sampleTimes.size() >= 2;
}

} // namespace vsr::io::usd
