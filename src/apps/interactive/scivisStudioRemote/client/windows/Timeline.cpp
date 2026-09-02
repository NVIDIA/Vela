// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Timeline.h"
#include "ReplicaView.h"
// vsr_scivis_studio_model
#include "Project.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>
// std
#include <algorithm>
#include <cstdio>

namespace vsr::scivis_studio::client {

using namespace std::chrono_literals;

namespace {

// How long a scrub is shown without the replica confirming it: the server
// debounces the snapshot by 250 ms, so well past that the replica wins.
constexpr auto SCRUB_SHOWN_FOR = 1500ms;

int lastFrameOf(const Shot &shot)
{
  return std::max(0, shot.frameCount - 1);
}

int clampFrame(const Shot &shot, int frame)
{
  return std::clamp(frame, 0, lastFrameOf(shot));
}

// Tick spacing in frames so labels stay at least ~60 px apart.
int tickIntervalFor(float pixelsPerFrame)
{
  const int candidates[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 500, 1000};
  for (int c : candidates) {
    if (c * pixelsPerFrame >= 60.f)
      return c;
  }
  return 1000;
}

} // namespace

Timeline::Timeline(vsr::ui::imgui::Application *app, EditorContext *context)
    : EditorWindow(app, context, "Timeline")
{}

Timeline::~Timeline() = default;

void Timeline::onProjectReplaced()
{
  m_draftStale = true;
}

// Shown time /////////////////////////////////////////////////////////////////

int Timeline::shownFrame(const Shot &shot) const
{
  if (m_dragFrame)
    return *m_dragFrame;
  if (shot.playing) {
    const auto &header = m_context->connection->lastFrameHeader();
    if (header && header->shotId == shot.id)
      return clampFrame(shot, header->frame);
    return shot.currentFrame;
  }
  if (m_scrubbedFrame)
    return *m_scrubbedFrame;
  return shot.currentFrame;
}

// Draft //////////////////////////////////////////////////////////////////////

void Timeline::syncDraft(const Shot &shot)
{
  const bool switched = !m_draft || m_draft->id != shot.id;
  const bool refresh = m_draftStale && !pending(m_pendingUpdate)
      && !ImGui::IsAnyItemActive();
  if (switched || refresh) {
    m_draft = shot;
    m_draftStale = false;
  }
}

void Timeline::sendDraft(int currentFrame)
{
  if (!m_draft || !canSend())
    return;
  Shot shot = *m_draft;
  shot.frameCount = std::max(1, shot.frameCount);
  shot.fps = std::max(1.f, shot.fps);
  // The server replaces its copy wholesale; carrying the frame it is showing
  // keeps an fps or loop edit from also jumping in time.
  shot.currentFrame = std::clamp(currentFrame, 0, shot.frameCount - 1);
  *m_draft = shot;
  m_pendingUpdate =
      ops().updateShot(shot, [this](const protocol::ProjectOpReply &reply) {
        if (!reply.ok)
          reportError(reply.error);
        m_draftStale = true;
      });
}

// Transport //////////////////////////////////////////////////////////////////

void Timeline::setPlaying(const Shot &shot, bool playing)
{
  if (!canSend() || pending(m_pendingPlaying))
    return;
  m_pendingPlaying = ops().setPlaying(shot.id, playing, errorReporter());
}

void Timeline::stop(const Shot &shot)
{
  if (!canSend())
    return;
  if (!shot.playing) {
    requestTime(0);
    return;
  }
  const ShotID shotId = shot.id;
  m_pendingPlaying = ops().setPlaying(
      shotId, false, [this, shotId](const protocol::ProjectOpReply &reply) {
        if (!reply.ok) {
          reportError(reply.error);
          return;
        }
        if (canSend())
          m_context->connection->setTime(shotId, 0);
        m_scrubbedFrame = 0;
        m_scrubbedAt = Clock::now();
      });
}

void Timeline::requestTime(int frame)
{
  m_timeRequest = frame;
}

void Timeline::flushTime(const Shot &shot)
{
  if (!m_timeRequest)
    return;
  const int frame = clampFrame(shot, *m_timeRequest);
  m_timeRequest.reset();
  if (!canSend())
    return;
  m_context->connection->setTime(shot.id, frame);
  m_scrubbedFrame = frame;
  m_scrubbedAt = Clock::now();
}

// UI /////////////////////////////////////////////////////////////////////////

void Timeline::buildEditorUI(const Project &project)
{
  const Shot *shot = replica::activeShot(project);
  if (!shot) {
    m_draft.reset();
    m_dragFrame.reset();
    m_scrubbedFrame.reset();
    ImGui::TextDisabled("No active shot");
    return;
  }
  syncDraft(*shot);

  // The replica caught up with the scrub, or long since should have.
  if (m_scrubbedFrame
      && (shot->playing || shot->currentFrame == *m_scrubbedFrame
          || Clock::now() - m_scrubbedAt > SCRUB_SHOWN_FOR))
    m_scrubbedFrame.reset();

  const int frame = shownFrame(*shot);

  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
      && !ImGui::GetIO().WantTextInput
      && ImGui::IsKeyPressed(ImGuiKey_Space, false))
    setPlaying(*shot, !shot->playing);

  buildUI_transport(*shot, frame);
  ImGui::Separator();
  buildUI_ruler(*shot, frame);

  flushTime(*shot);
}

void Timeline::buildUI_transport(const Shot &shot, int shownFrame)
{
  const float uiScale = ImGui::GetIO().FontGlobalScale;
  ImGui::PushStyleVar(
      ImGuiStyleVar_CellPadding, ImVec2(16.f * uiScale, 2.f * uiScale));

  if (ImGui::BeginTable("##controls",
          5,
          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableNextRow();
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding, ImVec2(8.f * uiScale, 4.f * uiScale));

    // Play/Pause and Stop: the button shows the replica's state, never an
    // assumed one.
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(pending(m_pendingPlaying));
    if (shot.playing) {
      if (ImGui::Button("||"))
        setPlaying(shot, false);
      vsr::ui::tooltipForPreviousItem("Pause (Space)");
    } else {
      if (ImGui::Button(" > "))
        setPlaying(shot, true);
      vsr::ui::tooltipForPreviousItem("Play (Space)");
    }
    ImGui::SameLine();
    if (ImGui::Button(" [] "))
      stop(shot);
    vsr::ui::tooltipForPreviousItem("Stop: pause and rewind to frame 0");
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(pending(m_pendingUpdate) || !m_draft);
    if (m_draft && ImGui::Checkbox("Loop", &m_draft->loop))
      sendDraft(shownFrame);

    // Frame counter: typed values commit once, on leaving the field.
    ImGui::TableNextColumn();
    int frame = shownFrame;
    ImGui::SetNextItemWidth(110.f * uiScale);
    if (ImGui::InputInt("##frame", &frame, 1, 10))
      requestTime(frame);
    ImGui::TableNextColumn();
    ImGui::Text("%d / %d", shownFrame, lastFrameOf(shot));

    // Frame count and fps //
    ImGui::TableNextColumn();
    if (m_draft) {
      ImGui::SetNextItemWidth(120.f * uiScale);
      ImGui::InputInt("Frames", &m_draft->frameCount, 1, 10);
      if (ImGui::IsItemDeactivatedAfterEdit())
        sendDraft(shownFrame);
    }
    ImGui::TableNextColumn();
    if (m_draft) {
      ImGui::SetNextItemWidth(90.f * uiScale);
      ImGui::InputFloat("FPS", &m_draft->fps, 0.f, 0.f, "%.1f");
      if (ImGui::IsItemDeactivatedAfterEdit())
        sendDraft(shownFrame);
    }
    ImGui::EndDisabled();

    ImGui::PopStyleVar();
    ImGui::EndTable();
  }

  ImGui::PopStyleVar();
}

void Timeline::buildUI_ruler(const Shot &shot, int shownFrame)
{
  const float uiScale = ImGui::GetIO().FontGlobalScale;
  const float rulerHeight = 28.f * uiScale;
  const float width = std::max(ImGui::GetContentRegionAvail().x, 40.f);
  const int lastFrame = lastFrameOf(shot);
  // The whole shot fits the ruler; the last frame sits at the right edge.
  const float pixelsPerFrame =
      lastFrame > 0 ? (width - 1.f) / float(lastFrame) : width;

  const ImVec2 rulerPos = ImGui::GetCursorScreenPos();
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(rulerPos,
      ImVec2(rulerPos.x + width, rulerPos.y + rulerHeight),
      IM_COL32(50, 50, 50, 255));

  const int tickInterval = tickIntervalFor(pixelsPerFrame);
  for (int f = 0; f <= lastFrame; f += tickInterval) {
    const float x = rulerPos.x + f * pixelsPerFrame;
    draw->AddLine(ImVec2(x, rulerPos.y + rulerHeight - 8.f * uiScale),
        ImVec2(x, rulerPos.y + rulerHeight),
        IM_COL32(200, 200, 200, 255));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", f);
    draw->AddText(ImVec2(x + 2.f * uiScale, rulerPos.y + 2.f * uiScale),
        IM_COL32(200, 200, 200, 255),
        buf);
  }

  const float scrubX = rulerPos.x + shownFrame * pixelsPerFrame;
  draw->AddLine(ImVec2(scrubX, rulerPos.y),
      ImVec2(scrubX, rulerPos.y + rulerHeight),
      IM_COL32(255, 80, 80, 255),
      2.f * uiScale);

  // One button over the whole ruler: a click seeks, holding drags.
  ImGui::InvisibleButton("##ruler", ImVec2(width, rulerHeight));
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const float mx = ImGui::GetMousePos().x - rulerPos.x;
    const int frame =
        clampFrame(shot, int(mx / pixelsPerFrame + 0.5f));
    if (!m_dragFrame || *m_dragFrame != frame)
      requestTime(frame);
    m_dragFrame = frame;
  } else if (m_dragFrame) {
    m_dragFrame.reset();
  }
  if (ImGui::IsItemHovered()) {
    const float mx = ImGui::GetMousePos().x - rulerPos.x;
    ImGui::SetTooltip(
        "frame %d", clampFrame(shot, int(mx / pixelsPerFrame + 0.5f)));
  }
}

} // namespace vsr::scivis_studio::client
