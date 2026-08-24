// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"

namespace vsr::ui::imgui {

struct ExportNanoVDBFileDialog : public Modal
{
  ExportNanoVDBFileDialog(Application *app);
  ~ExportNanoVDBFileDialog() override;

  void buildUI() override;

 private:
  std::string m_filename;
  int m_selectedFileType{0};
  bool m_enableUndefinedValue{false};
  float m_undefinedValue{0.0f};
  int m_precisionIndex{3}; // Default to Fp16 (index 3)
  bool m_enableDithering{false};
};

} // namespace vsr::ui::imgui
