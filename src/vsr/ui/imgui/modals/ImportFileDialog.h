// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Modal.h"

namespace vsr::ui::imgui {

struct ImportFileDialog : public Modal
{
  ImportFileDialog(Application *app);
  ~ImportFileDialog() override;

  void buildUI() override;

 private:
  std::string m_filename;
  int m_selectedFileType{0};
};

} // namespace vsr::ui::imgui
