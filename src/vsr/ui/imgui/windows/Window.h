// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_app
#include "vsr/app/Context.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/vsr_ui_imgui.h"
// imgui
#include <imgui.h>
// std
#include <string>

namespace vsr::ui::imgui {

class Application;

constexpr float INDENT_AMOUNT = 20.f;

struct Window
{
  Window(Application *app, const char *name);
  virtual ~Window();

  void renderUI();

  void show();
  void hide();
  void toggleShown();

  bool *visiblePtr();
  const char *name();

  // Interface to override for custom windows //

  virtual void buildUI() = 0;
  // Presentation settings: this window's part of the application's UI State
  // (docs/adr/0040), never a document's. Overrides chain to these, which
  // keep the window's visibility.
  virtual void saveSettings(vsr::core::DataNode &thisWindowRoot);
  virtual void loadSettings(vsr::core::DataNode &thisWindowRoot);
  // Settings that refer to the scene the window shows (a renderer, a camera
  // object): written into and read from an Application Dump. None by
  // default.
  virtual void saveSceneSettings(vsr::core::DataNode &thisWindowRoot);
  virtual void loadSceneSettings(vsr::core::DataNode &thisWindowRoot);

 protected:
  virtual int windowFlags() const;
  virtual int pushStyle();
  vsr::app::Context *appContext() const;
  // What the application lets the object editors offer; pass it to
  // buildUI_object()/buildUI_parameter().
  const vsr::ui::ObjectEditPolicy &objectEditPolicy() const;

  Application *m_app{nullptr};
  std::string m_name;
  bool m_visible{true};
};

} // namespace vsr::ui::imgui
