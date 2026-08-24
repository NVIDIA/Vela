// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_app
#include "vsr/app/Context.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/windows/Window.h"
// std
#include <string>
#include <vector>

namespace vsr::ui::imgui {

struct Animations : public Window
{
  Animations(Application *app, const char *name = "Animations");

  void buildUI() override;

 private:
  void buildUI_animationControls();
};

} // namespace vsr::ui::imgui
