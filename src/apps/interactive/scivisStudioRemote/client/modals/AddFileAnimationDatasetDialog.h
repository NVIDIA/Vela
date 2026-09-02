// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client
#include "EditorContext.h"
#include "RemoteBrowseDialog.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

/*
 * Add File Animation Dataset: an ordered list of server frame files (added
 * through Remote Browse in multi-file mode), reorderable and naturally
 * sortable, with the name auto-generated from the common stem until the
 * user edits it. Frames are not stat'ed here -- they live on the server --
 * but a mixed set of extensions is still pointed out. Import sends
 * ImportFileAnimationDataset (always VOLUME_ANIMATION, adopting the frame
 * count into the active shot) and keeps the dialog open, greyed, until the
 * reply.
 */
struct AddFileAnimationDatasetDialog : public vsr::ui::imgui::Modal
{
  AddFileAnimationDatasetDialog(
      vsr::ui::imgui::Application *app, EditorContext *context);
  ~AddFileAnimationDatasetDialog() override;

 private:
  void buildUI() override;
  void buildUI_listControls();
  void buildUI_frameList();
  void submit();
  void reset();
  void updateGeneratedName();
  void updateExtensionWarning();

  EditorContext *m_context{nullptr};
  std::string m_name;
  bool m_nameEditedByUser{false};
  std::vector<std::string> m_sourcePaths;
  std::vector<char> m_selectedRows;
  std::string m_extensionWarning;
  std::string m_error;
  RequestHandle m_pending;
  RemoteBrowseDialog m_browse;
};

} // namespace vsr::scivis_studio::client
