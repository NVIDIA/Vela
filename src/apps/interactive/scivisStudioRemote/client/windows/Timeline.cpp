// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Timeline.h"
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

// Transport //////////////////////////////////////////////////////////////////

void Timeline::commit(const Shot &shot, const ShotPatch &patch)
{
  if (!m_context->canSend())
    return;
  protocol::UpdateShot update;
  update.shotId = shot.id;
  update.patch = patch;
  m_update.send(
      m_context->ops(), std::move(update), m_context->errorReporter());
}

void Timeline::setPlaying(const Shot &shot, bool playing)
{
  if (!m_context->canSend())
    return;
  protocol::SetPlaying play;
  play.shotId = shot.id;
  play.playing = playing;
  m_playing.send(m_context->ops(), std::move(play), m_context->errorReporter());
}

void Timeline::stop(const Shot &shot)
{
  if (!m_context->canSend())
    return;
  if (!shot.playing) {
    requestTime(0);
    return;
  }
  const ShotID shotId = shot.id;
  protocol::SetPlaying pause;
  pause.shotId = shotId;
  pause.playing = false;
  m_playing.send(m_context->ops(),
      std::move(pause),
      [this, shotId](const protocol::ProjectOpReply &reply) {
        if (!reply.ok) {
          m_context->error(reply.error);
          return;
        }
        if (m_context->canSend())
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
  if (!m_context->canSend())
    return;
  m_context->connection->setTime(shot.id, frame);
  m_scrubbedFrame = frame;
  m_scrubbedAt = Clock::now();
}

// UI /////////////////////////////////////////////////////////////////////////

void Timeline::buildEditorUI(const Project &project)
{
  const Shot *shot = project::activeShot(project);
  if (!shot) {
    m_dragFrame.reset();
    m_scrubbedFrame.reset();
    ImGui::TextDisabled("No active shot");
    return;
  }

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
    ProjectOps &ops = m_context->ops();
    ImGui::BeginDisabled(m_playing.busy(ops));
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
    ImGui::BeginDisabled(m_update.busy(ops));
    bool loop = shot.loop;
    if (ImGui::Checkbox("Loop", &loop)) {
      ShotPatch patch;
      patch.loop = loop;
      commit(shot, patch);
    }

    // Frame counter: typed values commit once, on leaving the field (or
    // Enter), like the Frames and FPS fields; the +/- steps commit on release.
    ImGui::TableNextColumn();
    int frame = 0;
    ImGui::SetNextItemWidth(110.f * uiScale);
    if (m_frameField.draw("##frame", shownFrame, frame, 1, 10))
      requestTime(frame);
    ImGui::TableNextColumn();
    ImGui::Text("%d / %d", shownFrame, lastFrameOf(shot));

    buildUI_clock(shot);
    ImGui::EndDisabled();

    ImGui::PopStyleVar();
    ImGui::EndTable();
  }

  ImGui::PopStyleVar();
}

// Frame count and fps: each control shows the replica's value this UI frame
// (ImGui or an IntField holds an edit in progress) and commits a patch of
// its field alone, so the frame time rests on, a scrub included, is never in
// it.
void Timeline::buildUI_clock(const Shot &shot)
{
  const float uiScale = ImGui::GetIO().FontGlobalScale;
  ImGui::TableNextColumn();
  int frameCount = 0;
  ImGui::SetNextItemWidth(120.f * uiScale);
  if (m_frameCountField.draw("Frames", shot.frameCount, frameCount, 1, 10)) {
    ShotPatch patch;
    patch.frameCount = frameCount;
    commit(shot, patch);
  }
  ImGui::TableNextColumn();
  float fps = shot.fps;
  ImGui::SetNextItemWidth(90.f * uiScale);
  ImGui::InputFloat("FPS", &fps, 0.f, 0.f, "%.1f");
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    ShotPatch patch;
    patch.fps = fps;
    commit(shot, patch);
  }
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
    const int frame = clampFrame(shot, int(mx / pixelsPerFrame + 0.5f));
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
