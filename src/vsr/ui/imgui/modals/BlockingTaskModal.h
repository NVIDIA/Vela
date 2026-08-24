// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"
// vsr_core
#include "vsr/core/TaskQueue.hpp"
#include "vsr/core/Timer.hpp"
// std
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace vsr::ui::imgui {

struct BlockingTaskModal : public Modal
{
  BlockingTaskModal(Application *app);
  ~BlockingTaskModal() override;

  void buildUI() override;

  void activate(vsr::core::Future &&f, const char *text = "Please Wait");
  void activate(vsr::core::Future &&f,
      const char *text,
      std::shared_ptr<std::atomic_bool> cancelRequested);

 private:
  bool userClosable() const override;

  struct RunningTask
  {
    vsr::core::Future future;
    std::string text;
    std::shared_ptr<std::atomic_bool> cancelRequested;
  };

  std::deque<RunningTask> m_tasks;
  vsr::core::Timer m_timer;
  std::mutex m_mutex;
};

} // namespace vsr::ui::imgui
