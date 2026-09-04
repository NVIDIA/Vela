// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_protocol
#include "PlaybackMessages.h"
// vsr_scivis_studio_model
#include "ProjectContext.h"
// vsr_network
#include "vsr/network/NetworkChannel.hpp"
// vsr_app
#include "vsr/app/Context.h"
// std
#include <chrono>
#include <functional>
#include <optional>

namespace vsr::scivis_studio::server {

/*
 * The server's playback clock over the active shot: free-running Time in
 * Motion and the rest-commit of a scrub. Loop thread only.
 *
 * Each loop iteration ticks the AnimationManager by a steady-clock delta
 * (at most one frame) before the render, so the Frame header names the frame
 * drawn. Time at Rest reaches the client through snapshots only, and the
 * server decides those from the ProjectContext's revision: a play->stop flip
 * of the active shot (auto-stop) is a revision the context marks itself; a
 * SetTime scrub while paused opens a 250 ms quiet window and, once no SetTime
 * has arrived for that long and the frame differs from the one the window
 * opened on, commitScrubIfQuiet() marks the revision (a scrub that returns
 * to its start commits nothing). Frames a file binding cannot load go out
 * as TimeAdvanceWarning; playback goes on.
 *
 * A scrub window belongs to the session that opened it: the server cancels
 * it when a session begins or ends, so a window is never open without a
 * session up.
 *
 * Example (in the loop):
 *   playback.tick(sessionUp);
 *   playback.commitScrubIfQuiet();
 *   followProjectRevisions(); // the snapshot the commit asked for
 */
struct Playback
{
  using SendFn = std::function<void(vsr::network::Message &&)>;

  // `send` carries the TimeAdvanceWarnings; the contexts outlive this.
  Playback(vsr::app::Context &ctx, ProjectContext &projectContext, SendFn send);

  // Seeks the active shot to the latched SetTime; other shots are logged and
  // ignored. While paused it opens (or extends) the rest-commit window. The
  // warnings go out only with a session up: a SetTime that shares a batch
  // with the client's Hello seeks silently (its Bootstrap carries the frame).
  void applyTime(const protocol::SetTime &time, bool sessionUp);
  // One tick per iteration. The clock advances regardless (so the first tick
  // of a new session sees a small delta), but time moves and warnings go
  // out only with a session up.
  void tick(bool sessionUp);
  // Commits Time at Rest (markRevised) once the scrub window has been quiet
  // for SCRUB_COMMIT_QUIET and the frame differs from the one it opened on.
  void commitScrubIfQuiet();
  // Forgets an open scrub window, with the session that opened it.
  void cancelScrub();

 private:
  // One TimeAdvanceWarning per load failure the manager collected; without a
  // session up the failures are dropped (they were the seek's, not a tick's).
  void pushLoadFailures(bool sessionUp);

  using Clock = std::chrono::steady_clock;

  vsr::app::Context &m_ctx;
  ProjectContext &m_projectContext;
  SendFn m_send;

  std::optional<Clock::time_point> m_lastTick;
  bool m_scrubPending{false};
  Clock::time_point m_scrubDeadline{};
  int m_scrubFrameBefore{0}; // the frame time rested on when the window opened
};

} // namespace vsr::scivis_studio::server
