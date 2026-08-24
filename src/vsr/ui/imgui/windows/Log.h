// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Window.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <array>
#include <mutex>

namespace vsr::ui::imgui {

struct Log : public Window
{
  Log(Application *app, bool installAsLoggingTarget = true);
  ~Log();

  void buildUI() override;

 private:
  void addText(vsr::core::LogLevel level, const std::string &msg);
  void showLine(int line_no, bool useFilter);
  void clear();

  // Data //

  bool m_isLoggingTarget{false};
  std::mutex m_mutex;

  ImGuiTextBuffer m_buf;
  ImGuiTextFilter m_filter;
  ImVector<int> m_lineOffsets;
  ImVector<int> m_colorIDs;

  std::array<ImVec4, 7> m_colors;

  bool m_autoScroll{true};
};

} // namespace vsr::ui::imgui
