// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ProjectContext.h"
#include "vsr/ui/imgui/modals/Modal.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace vsr::scivis_studio {

struct AddFileAnimationDatasetDialog : public vsr::ui::imgui::Modal
{
  AddFileAnimationDatasetDialog(
      vsr::ui::imgui::Application *app, ProjectContext *projectContext);
  ~AddFileAnimationDatasetDialog() override;

 private:
  void buildUI() override;
  void reset();
  void clearValidation();
  void appendBrowsedFiles();
  void updateGeneratedName();
  bool validateForImport();

  ProjectContext *m_projectContext{nullptr};
  std::array<char, 512> m_name{};
  std::vector<std::string> m_sourcePaths;
  std::vector<std::string> m_browsedSourcePaths;
  std::vector<bool> m_invalidRows;
  std::vector<char> m_selectedRows;
  std::string m_validationMessage;
  std::string m_extensionWarning;
  bool m_nameEditedByUser{false};
  bool m_showInvalidRows{false};
};

} // namespace vsr::scivis_studio
