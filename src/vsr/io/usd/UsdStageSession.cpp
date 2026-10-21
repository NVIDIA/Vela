// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/usd/UsdStageSession.h"
#include "vsr/core/Logging.hpp"
// usd
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdsi/implicitSurfaceSceneIndex.h>
#include <pxr/imaging/hdsi/nurbsApproximatingSceneIndex.h>
#include <pxr/imaging/hdsi/pinnedCurveExpandingSceneIndex.h>
#include <pxr/imaging/hdsi/tetMeshConversionSceneIndex.h>
#include <pxr/usd/usd/editContext.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usdImaging/usdImaging/sceneIndices.h>
// std
#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

namespace vsr::io::usd {

namespace {

// Everything OpenUSD can resolve for us, resolved before VSR sees it: sphere,
// cone and cylinder stay analytic for VSR's native quadrics while capsule,
// cube and plane become meshes; NURBS are approximated; pinned curves are
// expanded; tetrahedral meshes are converted.
pxr::HdSceneIndexBaseRefPtr buildFilterChain(pxr::HdSceneIndexBaseRefPtr input)
{
  using pxr::HdsiImplicitSurfaceSceneIndexTokens;

  auto implicitArgs =
      pxr::HdRetainedContainerDataSource::New(pxr::HdPrimTypeTokens->capsule,
          pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(
              HdsiImplicitSurfaceSceneIndexTokens->toMesh),
          pxr::HdPrimTypeTokens->cube,
          pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(
              HdsiImplicitSurfaceSceneIndexTokens->toMesh),
          pxr::HdPrimTypeTokens->plane,
          pxr::HdRetainedTypedSampledDataSource<pxr::TfToken>::New(
              HdsiImplicitSurfaceSceneIndexTokens->toMesh));
  // Cone and cylinder are deliberately left alone: axisToTransform would move
  // the shape's spine into a transform this importer does not read (local
  // transforms come from the Stage, not the resolved scene), so the converter
  // folds the axis into the emitted endpoints instead.

  auto retval = pxr::HdsiImplicitSurfaceSceneIndex::New(input, implicitArgs);
  auto nurbs = pxr::HdsiNurbsApproximatingSceneIndex::New(retval);
  auto curves = pxr::HdsiPinnedCurveExpandingSceneIndex::New(nurbs);
  return pxr::HdsiTetMeshConversionSceneIndex::New(curves);
}

// The registry key. A Stage that cannot be resolved to an absolute path is
// keyed by what the caller said, which is still stable within one process.
// Variant Selections are part of it because they change what the Stage
// composes to: the same file opened with different selections is a different
// Stage. The map is ordered, so equal selections always make equal keys.
std::string sessionKeyOf(
    const std::string &filePath, const UsdVariantSelections &variantSelections)
{
  std::error_code ec;
  auto absolute = std::filesystem::weakly_canonical(filePath, ec);
  auto retval = ec ? filePath : absolute.string();
  for (const auto &[set, variant] : variantSelections)
    retval += "\n" + set + "=" + variant;
  return retval;
}

std::mutex &registryMutex()
{
  static std::mutex mutex;
  return mutex;
}

std::map<std::string, std::weak_ptr<UsdStageSession>> &registry()
{
  static std::map<std::string, std::weak_ptr<UsdStageSession>> sessions;
  return sessions;
}

} // namespace

UsdStageSession::UsdStageSession(std::string key,
    std::string filePath,
    UsdVariantSelections variantSelections,
    pxr::UsdStageRefPtr stage)
    : m_key(std::move(key)),
      m_filePath(std::move(filePath)),
      m_variantSelections(std::move(variantSelections)),
      m_stage(std::move(stage))
{
  // Before the resolution chain is built on the Stage, so the chain never sees
  // a variant that is about to be deselected.
  selectVariants();

  pxr::UsdImagingCreateSceneIndicesInfo createInfo;
  createInfo.stage = m_stage;
  createInfo.addDrawModeSceneIndex = false;
  auto sceneIndices = pxr::UsdImagingCreateSceneIndices(createInfo);
  m_stageSceneIndex = sceneIndices.stageSceneIndex;
  m_sceneIndex = buildFilterChain(sceneIndices.finalSceneIndex);

  // Values authored only as time samples do not resolve at UsdTimeCode's
  // default, so a Stage with no authored range is still read at a real time.
  m_authoredTimeRange = m_stage->HasAuthoredTimeCodeRange();
  if (m_authoredTimeRange) {
    m_startTimeCode = m_stage->GetStartTimeCode();
    m_endTimeCode = m_stage->GetEndTimeCode();
    if (!(m_endTimeCode > m_startTimeCode))
      m_endTimeCode = m_startTimeCode;
  }

  setTime(pxr::UsdTimeCode(m_startTimeCode));
}

UsdStageSession::~UsdStageSession()
{
  // Take the registry entry out with the Session, so a later acquire of this
  // file opens a fresh Stage rather than finding a corpse. The entry may
  // already have been replaced by a newer Session for the same file, which
  // `expired()` distinguishes.
  std::lock_guard<std::mutex> guard(registryMutex());
  auto &sessions = registry();
  if (auto found = sessions.find(m_key);
      found != sessions.end() && found->second.expired())
    sessions.erase(found);
}

const std::string &UsdStageSession::filePath() const
{
  return m_filePath;
}

const UsdVariantSelections &UsdStageSession::variantSelections() const
{
  return m_variantSelections;
}

const pxr::UsdStageRefPtr &UsdStageSession::stage() const
{
  return m_stage;
}

size_t UsdStageSession::variantSelectedPrims() const
{
  return m_variantSelectedPrims;
}

const std::vector<std::string> &UsdStageSession::unavailableVariants() const
{
  return m_unavailableVariants;
}

// Make the requested selections in the Stage's session layer -- stronger than
// any opinion in the files, and never written back to them -- and only then
// load payloads, so a deselected variant's payload is never composed. Loading
// can reveal Variant Sets the payloads themselves author, so payloads load one
// level per pass, without their descendants, and selecting and loading
// alternate until a pass does neither: a Variant Set a payload brings in is
// selected before any payload inside its variants loads.
//
// Prims inside a native USD Instance are not visited: an instance proxy cannot
// carry an opinion of its own. A Variant Set on the instanceable prim itself
// is visited like any other.
void UsdStageSession::selectVariants()
{
  if (m_variantSelections.empty())
    return;

  pxr::UsdEditContext editInSessionLayer(m_stage, m_stage->GetSessionLayer());

  constexpr int MAX_PASSES = 32;
  for (int pass = 0; pass < MAX_PASSES; ++pass) {
    // Paths rather than prims: authoring a selection recomposes the prim's
    // subtree, which would invalidate a traversal still walking it.
    std::vector<
        std::pair<pxr::SdfPath, const UsdVariantSelections::value_type *>>
        pending;
    for (const auto &prim : m_stage->Traverse(pxr::UsdPrimAllPrimsPredicate)) {
      auto variantSets = prim.GetVariantSets();
      for (const auto &selection : m_variantSelections) {
        const auto &[set, variant] = selection;
        if (!variantSets.HasVariantSet(set))
          continue;
        auto variantSet = variantSets.GetVariantSet(set);
        if (variantSet.GetVariantSelection() != variant
            && variantSet.HasAuthoredVariant(variant))
          pending.emplace_back(prim.GetPath(), &selection);
      }
    }

    for (const auto &[path, selection] : pending) {
      // An earlier selection on an ancestor may have composed this prim away.
      if (auto prim = m_stage->GetPrimAtPath(path)) {
        prim.GetVariantSets()
            .GetVariantSet(selection->first)
            .SetVariantSelection(selection->second);
      }
    }

    const auto loaded = m_stage->GetLoadSet();
    pxr::SdfPathSet toLoad;
    for (const auto &path : m_stage->FindLoadable()) {
      if (loaded.count(path) == 0)
        toLoad.insert(path);
    }
    if (pending.empty() && toLoad.empty())
      break;
    m_stage->LoadAndUnload(toLoad, {}, pxr::UsdLoadWithoutDescendants);
  }

  std::set<std::string> setsSeen;
  for (const auto &prim : m_stage->Traverse(pxr::UsdPrimAllPrimsPredicate)) {
    auto variantSets = prim.GetVariantSets();
    bool selected = false;
    for (const auto &[set, variant] : m_variantSelections) {
      if (!variantSets.HasVariantSet(set))
        continue;
      setsSeen.insert(set);
      auto variantSet = variantSets.GetVariantSet(set);
      if (variantSet.GetVariantSelection() == variant)
        selected = true;
      else if (!variantSet.HasAuthoredVariant(variant)) {
        m_unavailableVariants.push_back(
            prim.GetPath().GetString() + " {" + set + "=" + variant + "}");
      }
    }
    if (selected)
      m_variantSelectedPrims++;
  }

  for (const auto &[set, variant] : m_variantSelections) {
    if (setsSeen.count(set) == 0) {
      core::logWarning(
          "[UsdStageSession] %s: no prim has a Variant Set '%s';"
          " selection '%s' had no effect",
          m_filePath.c_str(),
          set.c_str(),
          variant.c_str());
    }
  }
}

const pxr::HdSceneIndexBaseRefPtr &UsdStageSession::sceneIndex() const
{
  return m_sceneIndex;
}

double UsdStageSession::startTimeCode() const
{
  return m_startTimeCode;
}

double UsdStageSession::endTimeCode() const
{
  return m_endTimeCode;
}

double UsdStageSession::timeCodesPerSecond() const
{
  return m_stage->GetTimeCodesPerSecond();
}

bool UsdStageSession::hasAuthoredTimeRange() const
{
  return m_authoredTimeRange;
}

void UsdStageSession::noteAuthoredSampleTimes(const std::vector<double> &times)
{
  if (m_authoredTimeRange || times.empty())
    return;

  if (!m_sawSampleTimes) {
    m_sawSampleTimes = true;
    m_startTimeCode = times.front();
    m_endTimeCode = times.back();
    return;
  }
  m_startTimeCode = std::min(m_startTimeCode, times.front());
  m_endTimeCode = std::max(m_endTimeCode, times.back());
}

pxr::UsdTimeCode UsdStageSession::timeCodeAt(float t) const
{
  const double span = m_endTimeCode - m_startTimeCode;
  return pxr::UsdTimeCode(m_startTimeCode + double(t) * span);
}

pxr::UsdTimeCode UsdStageSession::currentTime() const
{
  return m_currentTime;
}

void UsdStageSession::setTime(pxr::UsdTimeCode time)
{
  if (m_currentTime == time)
    return;
  m_currentTime = time;
  if (!m_stageSceneIndex)
    return;
  m_stageSceneIndex->SetTime(time);
  m_stageSceneIndex->ApplyPendingUpdates();
}

std::shared_ptr<UsdStageSession> acquireUsdSession(
    const std::string &filePath, const UsdVariantSelections &variantSelections)
{
  const auto key = sessionKeyOf(filePath, variantSelections);

  std::lock_guard<std::mutex> guard(registryMutex());

  auto &sessions = registry();
  if (auto found = sessions.find(key); found != sessions.end()) {
    if (auto existing = found->second.lock())
      return existing;
  }

  // With selections to make, nothing loads until they are made: the Session
  // loads payloads itself once the deselected ones can no longer be reached.
  auto stage = pxr::UsdStage::Open(filePath,
      variantSelections.empty() ? pxr::UsdStage::LoadAll
                                : pxr::UsdStage::LoadNone);
  if (!stage)
    return {};

  auto session = std::make_shared<UsdStageSession>(
      key, filePath, variantSelections, stage);
  sessions[key] = session;
  return session;
}

} // namespace vsr::io::usd
