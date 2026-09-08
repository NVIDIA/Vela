// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_modals
#include "modals/BrowseProvider.h"
#include "modals/ModalAction.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <cstddef>
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
 * exist is the Action's business. A host that can tell names the offending
 * frames (Action::unreadableFrames) and they are marked red in the list;
 * anything else it refuses is one message under the form.
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
    // Which frames of `request` the host cannot read, as indices into its
    // sourcePaths. A host holding the files (the monolith) stats them before
    // anything is imported, and the dialog marks those rows red instead of
    // submitting; a host whose frames are the server's cannot tell, returns
    // none, and answers a submit with the server's error instead. Empty
    // means "nothing known against them", not "all readable".
    virtual std::vector<size_t> unreadableFrames(const Request &request) const;
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
  void clearValidation();
  void updateGeneratedName();
  void updateExtensionWarning();

  std::unique_ptr<BrowseProvider> m_browse;
  std::unique_ptr<Action> m_action;
  std::string m_name;
  bool m_nameEditedByUser{false};
  std::vector<std::string> m_sourcePaths;
  std::vector<char> m_selectedRows;
  // Set for a frame the Action named unreadable at the last Import; the row
  // is drawn red until the list changes.
  std::vector<char> m_invalidRows;
  std::string m_extensionWarning;
  std::string m_error;
};

} // namespace vsr::scivis_studio::modals
