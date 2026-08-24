// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"

namespace vsr::ui::imgui {

struct IsosurfaceEditor : public Window
{
  IsosurfaceEditor(Application *app, const char *name = "Isosurface Editor");
  void buildUI() override;

 private:
  void addIsosurfaceGeometryFromSelected();
};

} // namespace vsr::ui::imgui
