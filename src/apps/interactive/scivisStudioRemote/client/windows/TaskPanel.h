// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client
#include "EditorContext.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/windows/Window.h"

namespace vsr::scivis_studio::client {

/*
 * The Server Tasks this client knows about, one row per TaskRecord: label,
 * state, progress (indeterminate while the server reports no total), the
 * last message, and the error of a failed task. Cancel is offered only for a
 * Queued task -- the running one cannot be interrupted in this milestone --
 * and "Clear finished" drops completed and failed rows. Records are the
 * connection's; disconnecting empties the panel.
 */
struct TaskPanel : public vsr::ui::imgui::Window
{
  TaskPanel(vsr::ui::imgui::Application *app, EditorContext *context);
  ~TaskPanel() override;

  void buildUI() override;

 private:
  EditorContext *m_context{nullptr};
};

} // namespace vsr::scivis_studio::client
