// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TaskPanel.h"
// scivisStudioClient
#include "UICommon.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>
// std
#include <algorithm>
#include <string>

namespace vsr::scivis_studio::client {

TaskPanel::TaskPanel(vsr::ui::imgui::Application *app, EditorContext *context)
    : Window(app, "Tasks"), m_context(context)
{}

TaskPanel::~TaskPanel() = default;

void TaskPanel::buildUI()
{
  auto *connection = m_context->connection;
  if (!connection || connection->state() == ConnectionState::NeverConnected
      || connection->state() == ConnectionState::Disconnected) {
    ImGui::TextDisabled("Not connected");
    return;
  }

  auto &ops = connection->projectOps();
  const auto &tasks = ops.tasks();
  const bool anyFinished = std::any_of(
      tasks.begin(), tasks.end(), [](const auto &t) { return t.finished(); });

  ImGui::BeginDisabled(!anyFinished);
  if (ImGui::Button("Clear finished"))
    ops.clearFinishedTasks();
  ImGui::EndDisabled();

  if (tasks.empty()) {
    ImGui::TextDisabled("No tasks");
    return;
  }

  const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
      | ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("tasks", 5, flags))
    return;

  ImGui::TableSetupColumn("Task", ImGuiTableColumnFlags_WidthStretch, 2.f);
  ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthStretch, 1.f);
  ImGui::TableSetupColumn("Progress", ImGuiTableColumnFlags_WidthStretch, 2.f);
  ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 3.f);
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70.f);
  ImGui::TableHeadersRow();

  const bool canCancel = m_context->canSend();
  for (const auto &task : tasks) {
    ImGui::PushID(int(task.taskId));
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        task.label.empty() ? "<task>" : task.label.c_str());
    vsr::ui::tooltipForPreviousItem(
        ("task id " + std::to_string(task.taskId)).c_str());

    ImGui::TableNextColumn();
    if (task.state == TaskState::Failed)
      ImGui::TextColored(ui::ERROR_TEXT_COLOR, "%s", toString(task.state));
    else
      ImGui::TextUnformatted(toString(task.state));

    ImGui::TableNextColumn();
    const auto &progress = task.lastProgress;
    switch (task.state) {
    case TaskState::Queued:
      ImGui::ProgressBar(0.f, ImVec2(-FLT_MIN, 0.f), "queued");
      break;
    case TaskState::Running:
      if (progress.total == 0) {
        // A negative fraction animates ImGui's indeterminate bar.
        ImGui::ProgressBar(
            -1.f * float(ImGui::GetTime()), ImVec2(-FLT_MIN, 0.f), "");
      } else {
        const float fraction =
            float(double(progress.current) / double(progress.total));
        const std::string overlay = std::to_string(progress.current) + "/"
            + std::to_string(progress.total);
        ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.f), overlay.c_str());
      }
      break;
    case TaskState::Completed:
      ImGui::ProgressBar(1.f, ImVec2(-FLT_MIN, 0.f), "done");
      break;
    case TaskState::Failed:
      ImGui::ProgressBar(0.f, ImVec2(-FLT_MIN, 0.f), "failed");
      break;
    }

    ImGui::TableNextColumn();
    if (task.state == TaskState::Failed) {
      ImGui::TextColored(ui::ERROR_TEXT_COLOR, "%s", task.error.c_str());
    } else if (task.framesCompleted != 0) {
      ImGui::TextWrapped("%llu frames  %s",
          static_cast<unsigned long long>(task.framesCompleted),
          progress.message.c_str());
    } else {
      ImGui::TextWrapped("%s", progress.message.c_str());
    }

    ImGui::TableNextColumn();
    if (!task.finished()) {
      ImGui::BeginDisabled(!canCancel);
      if (ImGui::SmallButton("Cancel"))
        ops.cancelTask(task.taskId, m_context->errorReporter());
      ImGui::EndDisabled();
    }
    ImGui::PopID();
  }

  ImGui::EndTable();
}

} // namespace vsr::scivis_studio::client
