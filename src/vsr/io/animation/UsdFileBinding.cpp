// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/animation/UsdFileBinding.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
#include "vsr/core/Logging.hpp"
#if VSR_USE_USD
// vsr_io
#include "vsr/io/usd/UsdStageSession.h"
#endif

namespace vsr::io {

using namespace vsr::core;

UsdFileBinding::UsdFileBinding(scene::Scene *scene,
    std::shared_ptr<usd::UsdStageSession> session,
    std::string stageFile,
    std::string primPath)
    : FileBinding(scene),
      m_session(std::move(session)),
      m_stageFile(std::move(stageFile)),
      m_primPath(std::move(primPath))
{
#if VSR_USE_USD
  // A binding an Import created is handed the Session itself; the selections
  // it was opened with are kept beside the file so an Archive can rejoin it.
  if (m_session)
    m_variantSelections = m_session->variantSelections();
#endif
}

UsdFileBinding::~UsdFileBinding() = default;

const std::string &UsdFileBinding::stageFile() const
{
  return m_stageFile;
}

const std::string &UsdFileBinding::primPath() const
{
  return m_primPath;
}

const UsdVariantSelections &UsdFileBinding::variantSelections() const
{
  return m_variantSelections;
}

usd::UsdStageSession *UsdFileBinding::session() const
{
  return m_session.get();
}

void UsdFileBinding::writePathsToDataNode(core::DataNode &node) const
{
  node["stageFile"] = m_stageFile;
  node["primPath"] = m_primPath;
  if (!m_variantSelections.empty()) {
    auto &selectionsNode = node["variantSelections"];
    for (const auto &[set, variant] : m_variantSelections)
      selectionsNode[set] = variant;
  }
}

void UsdFileBinding::readVariantSelectionsFromDataNode(
    const core::DataNode &node)
{
  m_variantSelections.clear();
  if (const auto *selectionsNode = node.child("variantSelections")) {
    selectionsNode->foreach_child_const([&](const core::DataNode &n) {
      m_variantSelections[n.name()] = n.getValueOr<std::string>("");
    });
  }
}

#if VSR_USE_USD

bool UsdFileBinding::ensureSession()
{
  if (m_session)
    return true;
  if (m_sessionFailed)
    return false;

  m_session = usd::acquireUsdSession(m_stageFile, m_variantSelections);
  if (!m_session) {
    m_sessionFailed = true;
    logWarning(
        "[%s] failed to open stage '%s'", logTag(), m_stageFile.c_str());
    reportLoadFailure(0, "failed to open USD stage '" + m_stageFile + "'");
  }
  return bool(m_session);
}

void UsdFileBinding::noteAuthoredSampleTimes(const std::vector<double> &times)
{
  if (m_session)
    m_session->noteAuthoredSampleTimes(times);
}

#else

bool UsdFileBinding::ensureSession()
{
  if (m_sessionFailed)
    return false;
  m_sessionFailed = true;
  logError("[%s] USD not enabled in VSR build.", logTag());
  reportLoadFailure(
      0, "USD stage '" + m_stageFile + "' needs a build with VSR_USE_USD");
  return false;
}

void UsdFileBinding::noteAuthoredSampleTimes(const std::vector<double> &)
{
  // No Session to tell.
}

#endif

} // namespace vsr::io
