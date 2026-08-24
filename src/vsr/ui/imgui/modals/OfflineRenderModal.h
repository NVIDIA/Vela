// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"
// vsr_core
#include "vsr/core/TaskQueue.hpp"
#include "vsr/core/Timer.hpp"

namespace vsr::ui::imgui {

struct OfflineRenderModal : public Modal
{
  OfflineRenderModal(Application *app);
  ~OfflineRenderModal() override;

  void buildUI() override;
  void start();

 private:
  vsr::core::Future m_future;
  std::string m_text;
  vsr::core::Timer m_timer;
  bool m_canceled{false};
};

} // namespace vsr::ui::imgui
