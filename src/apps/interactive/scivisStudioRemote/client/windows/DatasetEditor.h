// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorWindow.h"
#include "RemoteBrowseDialog.h"
#include "UICommon.h"
// vsr_scivis_studio_protocol
#include "ProjectRequests.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// std
#include <string>
#include <vector>

namespace vsr::scivis_studio::client {

/*
 * The client's copy of the Dataset Editor: the inventory as the replica
 * shows it (status, residency, source, frames, root node) and every dataset
 * op -- rename, load/unload, reimport, remove, save/load archive, discover
 * and incorporate candidates -- as a Project Op, with the archive paths
 * chosen through Remote Browse. The selection is a DatasetID re-resolved
 * against each snapshot; a control with a request in flight is greyed until
 * the reply. An Unloaded dataset's availability is the server's to watch
 * (it stats the asset once a second and snapshots a change), so the editor
 * only shows the status the replica holds.
 */
struct DatasetEditor : public EditorWindow
{
  DatasetEditor(vsr::ui::imgui::Application *app, EditorContext *context);
  ~DatasetEditor() override;

  void onProjectReplaced() override;

 private:
  void buildEditorUI(const Project &project) override;
  void buildPopups(const Project &project) override;

  const Dataset *resolveSelection(const Project &project);
  void buildUI_toolbar(const Project &project);
  void buildUI_nameField(const Dataset &dataset);
  void buildUI_details(const Dataset &dataset);
  void buildUI_actions(const Project &project, const Dataset &dataset);
  void buildUI_discoveryReview();
  void buildUI_removeConfirmation(const Project &project);

  DatasetID m_selected;
  InFlight m_datasetOp; // load/unload/reimport/remove/archives
  InFlight m_rename;
  InFlight m_discover;

  ui::BufferedNameField m_nameField;

  DatasetID m_datasetToRemove;
  bool m_keepRemovedAsset{false};

  std::vector<protocol::DatasetCandidateEntry> m_candidates;
  std::vector<char> m_candidateSelected;
  std::vector<std::string> m_candidateNames;
  bool m_openReview{false};

  RemoteBrowseDialog m_browse;
};

} // namespace vsr::scivis_studio::client
