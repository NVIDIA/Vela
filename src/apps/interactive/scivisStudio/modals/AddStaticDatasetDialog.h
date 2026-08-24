// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ProjectContext.h"
#include "vsr/ui/imgui/modals/Modal.h"

#include <array>
#include <string>

namespace vsr::scivis_studio {

struct AddStaticDatasetDialog : public vsr::ui::imgui::Modal
{
  AddStaticDatasetDialog(
      vsr::ui::imgui::Application *app, ProjectContext *projectContext);
  ~AddStaticDatasetDialog() override;

 private:
  void buildUI() override;

  ProjectContext *m_projectContext{nullptr};
  std::array<char, 512> m_name{};
  std::array<char, 2048> m_sourcePath{};
  std::string m_browsedSourcePath;
  int m_selectedSource{0};
};

} // namespace vsr::scivis_studio
