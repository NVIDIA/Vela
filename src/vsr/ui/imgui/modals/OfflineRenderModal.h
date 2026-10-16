// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"
// vsr_app
#include "vsr/app/renderAnimationSequence.h"
// vsr_core
#include "vsr/core/TaskQueue.hpp"
#include "vsr/core/Timer.hpp"
// std
#include <atomic>

namespace vsr::ui::imgui {

struct OfflineRenderModal : public Modal
{
  OfflineRenderModal(Application *app);
  ~OfflineRenderModal() override;

  // Completion synchronizes worker state; nullptr while a job is running.
  const vsr::app::OfflineRenderResult *result() const;
  void buildUI() override;
  void start();
  void cancel();

 private:
  vsr::core::Future m_future;
  vsr::app::OfflineRenderResult m_result;
  vsr::core::Timer m_timer;
  std::atomic<bool> m_canceled{false};
  bool m_exception{false};
};

} // namespace vsr::ui::imgui
