// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/TypeMacros.hpp"
// std
#include <functional>

namespace vsr::render_cli {

/* Application-boundary orchestration only; the shared engine never uses MPI. */
struct RenderRuntime
{
  RenderRuntime(int &argc, char **&argv);
  ~RenderRuntime();
  VSR_NOT_COPYABLE(RenderRuntime)
  VSR_NOT_MOVEABLE(RenderRuntime)

  int rank() const;
  int size() const;
  void runPhase(const char *name, const std::function<void()> &work) const;
  [[noreturn]] void abort(const char *diagnostic) const;

 private:
  int m_rank{0};
  int m_size{1};
};

} // namespace vsr::render_cli
