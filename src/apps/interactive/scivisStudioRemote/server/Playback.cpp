// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Playback.h"
// vsr_scivis_studio_protocol
#include "StudioCodec.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <utility>

namespace vsr::scivis_studio::server {

using namespace protocol;

namespace {

// A paused scrub commits Time at Rest once SetTime has been quiet this long.
constexpr std::chrono::milliseconds SCRUB_COMMIT_QUIET{250};

} // namespace

Playback::Playback(
    vsr::app::Context &ctx, ProjectContext &projectContext, SendFn send)
    : m_ctx(ctx), m_projectContext(projectContext), m_send(std::move(send))
{}

void Playback::applyTime(const SetTime &time)
{
  const auto &project = m_projectContext.project();
  const auto *shot = project::activeShot(project);
  if (!shot)
    return;
  if (time.shotId != project.activeShotId) {
    vsr::core::logWarning(
        "[StudioServer] SetTime for shot '%s' ignored: the active shot is '%s'",
        time.shotId.c_str(),
        project.activeShotId.c_str());
    return;
  }

  if (!shot->playing) {
    if (!m_scrubPending)
      m_scrubFrameBefore = shot->currentFrame;
    m_scrubPending = true;
    m_scrubDeadline = Clock::now() + SCRUB_COMMIT_QUIET;
  }

  m_projectContext.setActiveShotFrame(time.frame);
  pushLoadFailures();
}

void Playback::tick(bool sessionUp)
{
  const auto now = Clock::now();
  float elapsed = 0.f;
  if (m_lastTick)
    elapsed = std::chrono::duration<float>(now - *m_lastTick).count();
  m_lastTick = now;

  if (!sessionUp)
    return;
  auto *shot = project::activeShot(m_projectContext.project());
  if (!shot)
    return;

  // A play commits the frame with its own snapshot; a scrub window left open
  // before it has nothing to add. Should the manager stop on its own during
  // the tick, the context marks the revision and the loop's rule snapshots.
  if (shot->playing)
    m_scrubPending = false;
  m_ctx.vsr.animationMgr.tick(elapsed);
  pushLoadFailures();
}

void Playback::commitScrubIfQuiet()
{
  if (!m_scrubPending || Clock::now() < m_scrubDeadline)
    return;
  m_scrubPending = false;

  const auto *shot = project::activeShot(m_projectContext.project());
  // A play that started meanwhile committed the frame with its own snapshot.
  if (!shot || shot->playing || shot->currentFrame == m_scrubFrameBefore)
    return;
  // Time at Rest: the frame the scrub ended on is project state now.
  m_projectContext.markRevised();
}

void Playback::cancelScrub()
{
  m_scrubPending = false;
}

void Playback::pushLoadFailures()
{
  auto failures = m_ctx.vsr.animationMgr.takeLoadFailures();
  if (failures.empty())
    return;
  const auto &project = m_projectContext.project();
  for (auto &failure : failures) {
    TimeAdvanceWarning warning;
    warning.shotId = project.activeShotId;
    warning.frame = failure.frame;
    warning.message = std::move(failure.message);
    m_send(encode(warning));
  }
}

} // namespace vsr::scivis_studio::server
