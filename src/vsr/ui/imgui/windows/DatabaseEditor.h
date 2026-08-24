// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"

namespace vsr::ui::imgui {

struct DatabaseEditor : public Window
{
  DatabaseEditor(Application *app, const char *name = "Database Editor");
  void buildUI() override;
};

} // namespace vsr::ui::imgui
