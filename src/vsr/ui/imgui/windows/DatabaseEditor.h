// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"

namespace vsr::ui::imgui {

struct DatabaseEditor : public Window
{
  DatabaseEditor(Application *app, const char *name = "Database Editor");
  void buildUI() override;

  // What the parameter widgets may offer; permissive by default. A host
  // whose scene is not its own to change (the SciVis Studio client, whose
  // edits must cross the wire) narrows it.
  void setEditPolicy(const vsr::ui::ObjectEditPolicy &policy);

 private:
  vsr::ui::ObjectEditPolicy m_editPolicy;
};

} // namespace vsr::ui::imgui
