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
{}

UsdFileBinding::~UsdFileBinding() = default;

const std::string &UsdFileBinding::stageFile() const
{
  return m_stageFile;
}

const std::string &UsdFileBinding::primPath() const
{
  return m_primPath;
}

usd::UsdStageSession *UsdFileBinding::session() const
{
  return m_session.get();
}

void UsdFileBinding::writePathsToDataNode(core::DataNode &node) const
{
  node["stageFile"] = m_stageFile;
  node["primPath"] = m_primPath;
}

#if VSR_USE_USD

bool UsdFileBinding::ensureSession()
{
  if (m_session)
    return true;
  if (m_sessionFailed)
    return false;

  m_session = usd::acquireUsdSession(m_stageFile);
  if (!m_session) {
    m_sessionFailed = true;
    logWarning(
        "[%s] failed to open stage '%s'", logTag(), m_stageFile.c_str());
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
  return false;
}

void UsdFileBinding::noteAuthoredSampleTimes(const std::vector<double> &)
{
  // No Session to tell.
}

#endif

} // namespace vsr::io
