// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OfflineRenderModal.h"
#include "vsr/ui/imgui/Application.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <exception>

namespace vsr::ui::imgui {

OfflineRenderModal::OfflineRenderModal(Application *app)
    : Modal(app, "##offline_render_modal")
{}

OfflineRenderModal::~OfflineRenderModal()
{
  cancel();
  vsr::core::wait(m_future);
}

const vsr::app::OfflineRenderResult *OfflineRenderModal::result() const
{
  return vsr::core::isReady(m_future) ? &m_result : nullptr;
}

void OfflineRenderModal::buildUI()
{
  if (vsr::core::isReady(m_future)) {
    if (m_result.status != vsr::app::OfflineRenderStatus::Failed) {
      this->hide();
      return;
    }
    ImGui::TextUnformatted("Offline rendering failed");
    ImGui::TextWrapped("%s", m_result.diagnostic.c_str());
    if (!m_result.destination.empty())
      ImGui::TextWrapped("Output: %s", m_result.destination.c_str());
    if (!m_exception)
      ImGui::Text("Completed frames: %zu", m_result.completedFrames);
    else if (m_result.frameIndex >= 0)
      ImGui::Text("Interrupted at frame: %d", m_result.frameIndex);
    if (ImGui::Button("close"))
      this->hide();
    return;
  }

  ImGui::ProgressBar(
      -1.0f * (float)ImGui::GetTime(), ImVec2(0.0f, 0.0f), "Rendering...");

  m_timer.end();
  ImGui::NewLine();
  ImGui::TextDisabled("elapsed time: %.2fs", m_timer.seconds());

  ImGui::Separator();
  ImGui::BeginDisabled(m_canceled.load());
  if (ImGui::Button("cancel"))
    cancel();
  ImGui::EndDisabled();
}

void OfflineRenderModal::cancel()
{
  m_canceled = true;
}

void OfflineRenderModal::start()
{
  if (!vsr::core::isReady(m_future))
    return;
  auto *ctx = appContext();
  m_timer.start();
  m_canceled = false;
  m_result = {};
  m_exception = false;
  this->show();
  m_future = m_app->enqueueTask([this, ctx]() {
    // The UI task is the application exception boundary. Always publish a
    // failure before the future becomes ready; wait() alone does not surface
    // exceptions captured by the task queue's packaged_task.
    try {
      ctx->initializeOfflineRenderer();
      m_result = vsr::app::renderAnimationSequence(*ctx,
          ctx->offline.output.outputDirectory,
          ctx->offline.output.filePrefix,
          [&](int frameIndex, int numFrames) {
            m_result.frameIndex = frameIndex;
            vsr::core::logStatus("[OfflineRenderModal] Rendering frame %d of %d",
                frameIndex + 1,
                numFrames);
            return !m_canceled.load();
          });
    } catch (const std::exception &error) {
      m_exception = true;
      m_result.status = vsr::app::OfflineRenderStatus::Failed;
      m_result.diagnostic =
          std::string("Offline rendering exception: ") + error.what();
    } catch (...) {
      m_exception = true;
      m_result.status = vsr::app::OfflineRenderStatus::Failed;
      m_result.diagnostic = "Unknown exception in offline rendering";
    }
    if (m_result.status == vsr::app::OfflineRenderStatus::Failed)
      vsr::core::logError("[OfflineRenderModal] %s: %s",
          m_result.destination.c_str(),
          m_result.diagnostic.c_str());
  });
}

} // namespace vsr::ui::imgui
