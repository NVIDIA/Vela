// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
// imgui
#include "imgui.h"

#include "vsr/app/Context.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"

namespace vsr::ui::imgui {

class Application;

struct Modal
{
  Modal(Application *app, const char *name);
  virtual ~Modal();

  void renderUI();

  void show();
  void hide();
  bool visible() const;

  const char *name() const;

 protected:
  virtual void buildUI() = 0;
  virtual bool userClosable() const;
  vsr::app::Context *appContext() const;
  // What the application lets the object editors offer; pass it to
  // buildUI_object()/buildUI_parameter().
  const vsr::ui::ObjectEditPolicy &objectEditPolicy() const;

  Application *m_app{nullptr};

 private:
  std::string m_name;
  bool m_visible{false};
};

} // namespace vsr::ui::imgui
