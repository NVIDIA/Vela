// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client
#include "EditorContext.h"
#include "RemoteBrowseDialog.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

/*
 * Add Static Dataset: name, server source path (typed or through Remote
 * Browse) and the explicit source choice -- one of the importers, a VSR
 * Dataset Archive or a VSR Layer Subtree Archive; nothing is inferred from
 * the extension. Import sends ImportStaticDataset or LoadDatasetArchive and
 * keeps the dialog open, greyed, until the reply: an accepted reply closes
 * it, a failed one shows the server's error. A name typed for an archive is
 * applied by RenameDataset once the snapshot after the load reveals the new
 * dataset's id (the protocol carries no name on LoadDatasetArchive).
 */
struct AddStaticDatasetDialog : public vsr::ui::imgui::Modal
{
  AddStaticDatasetDialog(
      vsr::ui::imgui::Application *app, EditorContext *context);
  ~AddStaticDatasetDialog() override;

  // Finishes a pending archive rename when the loaded dataset appears.
  void onProjectReplaced();

 private:
  void buildUI() override;
  void submit();
  void reset();

  EditorContext *m_context{nullptr};
  std::string m_name;
  std::string m_sourcePath;
  int m_selectedSource{0};
  std::string m_error;
  RequestHandle m_pending;
  RemoteBrowseDialog m_browse;

  // Archive load awaiting its rename: the dataset ids before the load, the
  // task, and the name to apply.
  struct ArchiveRename
  {
    uint64_t taskId{0};
    std::vector<DatasetID> datasetIdsBefore;
    std::string name;
  };
  std::optional<ArchiveRename> m_archiveRename;
};

} // namespace vsr::scivis_studio::client
