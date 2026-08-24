// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"

namespace vsr::ui::imgui {

struct CuttingPlaneDialog : public Modal
{
  CuttingPlaneDialog(Application *app);
  ~CuttingPlaneDialog() override = default;

  void buildUI() override;

 private:
  bool  m_enabled{false};
  float m_normal[3]{0.f, 0.f, 1.f};
  float m_d{0.f};
};

} // namespace vsr::ui::imgui
