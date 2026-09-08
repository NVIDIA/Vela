// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_modals
#include "modals/BrowseProvider.h"
#include "modals/ModalAction.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vsr::scivis_studio::modals {

/*
 * Add File Animation Dataset: an ordered list of frame files (added through
 * the host's BrowseProvider in multi-file mode), reorderable and naturally
 * sortable, with the name auto-generated from the common stem until the user
 * edits it. Frames are not stat'ed here -- the remote client's live on the
 * server -- but a mixed set of extensions is pointed out; whether the frames
 * exist is the Action's business, and its error shows in the dialog.
 */
struct AddFileAnimationDatasetDialog : public vsr::ui::imgui::Modal
{
  // The frames in the order the list shows them.
  struct Request
  {
    std::string name;
    std::vector<std::filesystem::path> sourcePaths;
  };

  struct Action : public ModalAction
  {
    virtual void submit(const Request &request, ActionResult done) = 0;
  };

  AddFileAnimationDatasetDialog(vsr::ui::imgui::Application *app,
      std::unique_ptr<BrowseProvider> browse,
      std::unique_ptr<Action> action);
  ~AddFileAnimationDatasetDialog() override;

 private:
  void buildUI() override;
  void buildUI_listControls();
  void buildUI_frameList();
  void submit();
  void reset();
  void updateGeneratedName();
  void updateExtensionWarning();

  std::unique_ptr<BrowseProvider> m_browse;
  std::unique_ptr<Action> m_action;
  std::string m_name;
  bool m_nameEditedByUser{false};
  std::vector<std::string> m_sourcePaths;
  std::vector<char> m_selectedRows;
  std::string m_extensionWarning;
  std::string m_error;
};

} // namespace vsr::scivis_studio::modals
