// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_app
#include "vsr/app/Context.h"
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
  virtual void saveSettings(vsr::core::DataNode &thisWindowRoot);
  virtual void loadSettings(vsr::core::DataNode &thisWindowRoot);

 protected:
  virtual int windowFlags() const;
  virtual int pushStyle();
  vsr::app::Context *appContext() const;

  Application *m_app{nullptr};
  std::string m_name;
  bool m_visible{true};
};

} // namespace vsr::ui::imgui
