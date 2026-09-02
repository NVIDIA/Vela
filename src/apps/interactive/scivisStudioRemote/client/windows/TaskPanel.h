// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorContext.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/windows/Window.h"

namespace vsr::scivis_studio::client {

/*
 * The Server Tasks this client knows about, one row per TaskRecord: label,
 * state, progress (determinate when the server reports a total, as a render
 * does per frame), the last message plus the frame count of a finished
 * render, and the error of a failed task. Cancel is offered for Queued and
 * Running tasks alike; the server decides (a queued task is removed, a
 * running render stops at its next frame, anything else is refused with a
 * toast). "Clear finished" drops completed and failed rows. Records are the
 * connection's; disconnecting empties the panel, a reconnect fails the open
 * ones with "connection lost" and the bootstrap's replay revives what the
 * server still knows.
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
