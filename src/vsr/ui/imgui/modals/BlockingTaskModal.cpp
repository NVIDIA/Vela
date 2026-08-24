// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "BlockingTaskModal.h"

namespace vsr::ui::imgui {

BlockingTaskModal::BlockingTaskModal(Application *app)
    : Modal(app, "##blocking_task_modal")
{}

BlockingTaskModal::~BlockingTaskModal() = default;

void BlockingTaskModal::buildUI()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_tasks.empty()) {
    this->hide();
    return;
  }

  auto *t = &m_tasks.front();
  while (vsr::core::isReady(t->future)) {
    m_tasks.pop_front();
    if (m_tasks.empty()) {
      this->hide();
      return;
    }
    t = &m_tasks.front();
  }

  ImGui::ProgressBar(
      -1.0f * (float)ImGui::GetTime(), ImVec2(0.0f, 0.0f), t->text.c_str());

  m_timer.end();
  ImGui::NewLine();
  ImGui::TextDisabled("elapsed time: %.2fs", m_timer.seconds());

  if (t->cancelRequested) {
    ImGui::Separator();
    const bool cancelRequested = t->cancelRequested->load();
    ImGui::BeginDisabled(cancelRequested);
    if (ImGui::Button("Cancel"))
      t->cancelRequested->store(true);
    ImGui::EndDisabled();
  }
}

void BlockingTaskModal::activate(vsr::core::Future &&f, const char *text)
{
  activate(std::move(f), text, {});
}

void BlockingTaskModal::activate(vsr::core::Future &&f,
    const char *text,
    std::shared_ptr<std::atomic_bool> cancelRequested)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_tasks.empty())
    m_timer.start();
  m_tasks.push_back({std::move(f), text, std::move(cancelRequested)});
  this->show();
}

bool BlockingTaskModal::userClosable() const
{
  return false;
}

} // namespace vsr::ui::imgui
