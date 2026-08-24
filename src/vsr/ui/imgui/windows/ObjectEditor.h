// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"

namespace vsr::ui::imgui {

struct ObjectEditor : public Window
{
  ObjectEditor(Application *app, const char *name = "Object Editor");
  void buildUI() override;
};

} // namespace vsr::ui::imgui
