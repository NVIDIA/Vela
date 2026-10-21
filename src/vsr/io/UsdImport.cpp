// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/UsdImport.hpp"
// vsr_animation
#include "vsr/animation/AnimationManager.hpp"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <string>

namespace vsr::io {

const char *toString(UsdMaterialMode mode)
{
  switch (mode) {
  case UsdMaterialMode::MATERIALX:
    return "materialx";
  case UsdMaterialMode::MDL:
    return "mdl";
  case UsdMaterialMode::PHYSICALLY_BASED:
  default:
    return "physicallyBased";
  }
}

UsdMaterialMode usdMaterialModeFromString(const std::string &name)
{
  if (name == "materialx")
    return UsdMaterialMode::MATERIALX;
  if (name == "mdl")
    return UsdMaterialMode::MDL;
  return UsdMaterialMode::PHYSICALLY_BASED;
}

std::string toString(const UsdVariantSelections &selections)
{
  std::string retval;
  for (const auto &[set, variant] : selections) {
    if (!retval.empty())
      retval += ',';
    retval += set + '=' + variant;
  }
  return retval;
}

UsdVariantSelections usdVariantSelectionsFromString(const std::string &text)
{
  UsdVariantSelections retval;
  size_t begin = 0;
  while (begin <= text.size()) {
    auto end = text.find(',', begin);
    if (end == std::string::npos)
      end = text.size();
    const auto entry = text.substr(begin, end - begin);
    const auto eq = entry.find('=');
    if (eq != std::string::npos && eq > 0 && eq + 1 < entry.size())
      retval[entry.substr(0, eq)] = entry.substr(eq + 1);
    begin = end + 1;
  }
  return retval;
}

// UsdImportOptions definitions ///////////////////////////////////////////////

void UsdImportOptions::toDataNode(core::DataNode &node) const
{
  auto &purposesNode = node["purposes"];
  purposesNode["default"] = purposes.defaultPurpose;
  purposesNode["render"] = purposes.render;
  purposesNode["proxy"] = purposes.proxy;
  purposesNode["guide"] = purposes.guide;

  auto &contextsNode = node["renderContexts"];
  for (const auto &context : renderContexts)
    contextsNode.append() = context;

  node["materialMode"] = std::string(toString(materialMode));
  node["refinementLevel"] = refinementLevel;
  node["primPath"] = primPath;

  auto &selectionsNode = node["variantSelections"];
  for (const auto &[set, variant] : variantSelections)
    selectionsNode[set] = variant;
}

void UsdImportOptions::fromDataNode(const core::DataNode &node)
{
  if (const auto *purposesNode = node.child("purposes")) {
    auto readFlag = [&](const char *name, bool &out) {
      if (const auto *n = purposesNode->child(name))
        out = n->getValueOr<bool>(out);
    };
    readFlag("default", purposes.defaultPurpose);
    readFlag("render", purposes.render);
    readFlag("proxy", purposes.proxy);
    readFlag("guide", purposes.guide);
  }

  if (const auto *contextsNode = node.child("renderContexts")) {
    renderContexts.clear();
    for (size_t i = 0; i < contextsNode->numChildren(); ++i)
      renderContexts.push_back(
          contextsNode->child(i)->getValueOr<std::string>(""));
  }

  if (const auto *n = node.child("materialMode"))
    materialMode = usdMaterialModeFromString(n->getValueOr<std::string>(""));
  if (const auto *n = node.child("refinementLevel"))
    refinementLevel = n->getValueOr<int>(refinementLevel);
  if (const auto *n = node.child("primPath"))
    primPath = n->getValueOr<std::string>(primPath);

  if (const auto *selectionsNode = node.child("variantSelections")) {
    variantSelections.clear();
    selectionsNode->foreach_child_const([&](const core::DataNode &n) {
      variantSelections[n.name()] = n.getValueOr<std::string>("");
    });
  }
}

// UsdImportReport definitions ////////////////////////////////////////////////

const char *toString(UsdSkipReason reason)
{
  switch (reason) {
  case UsdSkipReason::PURPOSE_EXCLUDED:
    return "purpose excluded";
  case UsdSkipReason::RESOLVED_INVISIBLE:
    return "resolved invisible";
  case UsdSkipReason::UNSUPPORTED_PRIM_TYPE:
    return "unsupported prim type";
  case UsdSkipReason::MATERIAL_RESOLUTION_FAILED:
    return "material resolution failed";
  case UsdSkipReason::TEXTURE_LOAD_FAILED:
    return "texture load failed";
  case UsdSkipReason::FIELD_LOAD_FAILED:
    return "field load failed";
  case UsdSkipReason::UNSUPPORTED_LIGHT_TYPE:
    return "unsupported light type";
  case UsdSkipReason::RICHER_MATERIAL_AVAILABLE:
    return "richer material network available";
  case UsdSkipReason::TIME_VARYING_VALUE_DROPPED:
    return "time-varying value dropped";
  case UsdSkipReason::COUNT:
    break;
  }
  return "unknown";
}

size_t UsdImportReport::countOf(UsdSkipReason reason) const
{
  return size_t(std::count_if(skipped.begin(),
      skipped.end(),
      [&](const UsdSkippedPrim &s) { return s.reason == reason; }));
}

bool UsdImportReport::contains(UsdSkipReason reason) const
{
  return countOf(reason) > 0;
}

std::string UsdImportReport::summary() const
{
  if (!stageOpened)
    return "stage failed to open";

  std::string retval = std::to_string(convertedPrims) + " prims converted, "
      + std::to_string(skipped.size()) + " skipped";

  if (animatedPrims > 0) {
    retval += ", " + std::to_string(animatedPrims) + " animated prims bound ("
        + std::to_string(sampleCount) + " samples @ "
        + std::to_string(int(timeCodesPerSecond)) + " fps)";
  }

  if (variantSelectedPrims > 0) {
    retval +=
        ", " + std::to_string(variantSelectedPrims) + " prims variant-selected";
  }
  if (!unavailableVariants.empty()) {
    retval += ", " + std::to_string(unavailableVariants.size())
        + " requested variants unavailable";
  }

  // Counts by reason, in enum order, omitting reasons that did not occur.
  bool first = true;
  for (int i = 0; i < int(UsdSkipReason::COUNT); ++i) {
    const auto reason = UsdSkipReason(i);
    const auto count = countOf(reason);
    if (count == 0)
      continue;
    retval += first ? " (" : ", ";
    retval += std::to_string(count) + " " + toString(reason);
    first = false;
  }
  if (!first)
    retval += ")";

  return retval;
}

void widenAnimationClock(
    vsr::animation::AnimationManager &animMgr, const UsdImportReport &report)
{
  if (report.sampleCount < 2)
    return;

  const int frames = int(report.sampleCount);
  const float fps = report.timeCodesPerSecond;
  if (animMgr.widenClock(frames, fps))
    return;

  core::logStatus(
      "[import_USD] stage wants %i frames at %g fps but the animation clock is"
      " already %i frames at %g fps; keeping the longer and faster of the two",
      frames,
      double(fps),
      animMgr.getAnimationTotalFrames(),
      double(animMgr.getAnimationFPS()));
}

} // namespace vsr::io
