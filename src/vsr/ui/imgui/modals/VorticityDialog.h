// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"
// std
#include <string>
#include <vector>

namespace vsr::ui::imgui {

struct VorticityDialog : public Modal
{
  VorticityDialog(Application *app);
  ~VorticityDialog() override = default;

  void buildUI() override;

 private:
  int m_uIdx{-1}; // index into m_fields list
  int m_vIdx{-1};
  int m_wIdx{-1};

  bool m_computeLambda2{true};
  bool m_computeQ{true};
  bool m_computeVorticity{true};
  bool m_computeHelicity{false};
};

} // namespace vsr::ui::imgui
