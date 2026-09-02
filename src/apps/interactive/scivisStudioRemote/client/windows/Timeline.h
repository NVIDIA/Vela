// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "EditorWindow.h"
// vsr_scivis_studio_model
#include "Shot.h"
// std
#include <chrono>
#include <optional>

namespace vsr::scivis_studio::client {

/*
 * The client's Timeline: the monolith's transport row and frame ruler
 * without tracks or keyframes, driving the server's free-running playback of
 * the active shot. There is no AnimationManager here. Time at Rest comes
 * from the Project Replica (`Shot::currentFrame`), Time in Motion from the
 * header of the last frame the viewport took, and a scrub in progress from
 * the drag itself; the play button follows the replica alone, so a refused
 * SetPlaying visibly stays put.
 *
 * Play/Pause is the SetPlaying project op; Stop is SetPlaying(false)
 * followed by SetTime 0; scrubbing and the frame field send SetTime, at most
 * once per UI frame with the latest value; Loop, frame count and fps travel
 * as UpdateShot with the whole shot. Disabled without an active shot or a
 * connection that can send.
 *
 * Example:
 *   auto *timeline = new Timeline(this, &m_editorContext);
 *   m_editors.push_back(timeline); // onProjectReplaced() fan-out
 */
struct Timeline : public EditorWindow
{
  Timeline(vsr::ui::imgui::Application *app, EditorContext *context);
  ~Timeline() override;

  void onProjectReplaced() override;

 private:
  using Clock = std::chrono::steady_clock;

  void buildEditorUI(const Project &project) override;
  void buildUI_transport(const Shot &shot, int shownFrame);
  void buildUI_ruler(const Shot &shot, int shownFrame);

  // What the scrubber and counter display this UI frame.
  int shownFrame(const Shot &shot) const;
  void syncDraft(const Shot &shot);
  void sendDraft(int currentFrame);
  void setPlaying(const Shot &shot, bool playing);
  void stop(const Shot &shot);
  // Coalesced: the latest request of a UI frame goes out at its end.
  void requestTime(int frame);
  void flushTime(const Shot &shot);

  // Loop, frame count and fps edits in progress; the rest of the Shot is
  // copied fresh from the replica when an edit commits.
  std::optional<Shot> m_draft;
  bool m_draftStale{true};
  RequestHandle m_pendingUpdate;
  RequestHandle m_pendingPlaying;

  // Scrubbing //
  std::optional<int> m_dragFrame; // the mouse is down on the ruler
  std::optional<int> m_timeRequest; // this UI frame's latest SetTime
  // The last scrub, shown until the replica agrees or the debounced
  // snapshot has surely come and gone.
  std::optional<int> m_scrubbedFrame;
  Clock::time_point m_scrubbedAt{};
};

} // namespace vsr::scivis_studio::client
