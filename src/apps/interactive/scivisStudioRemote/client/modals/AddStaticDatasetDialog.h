// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// scivisStudioClient
#include "EditorContext.h"
#include "RemoteBrowseDialog.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <string>

namespace vsr::scivis_studio::client {

/*
 * Add Static Dataset: name, server source path (typed or through Remote
 * Browse) and the explicit source choice -- one of the importers, a VSR
 * Dataset Archive or a VSR Layer Subtree Archive; nothing is inferred from
 * the extension. Import sends ImportStaticDataset, ImportSubtreeDataset or
 * LoadDatasetArchive (each carrying the typed name) and keeps the dialog
 * open, greyed, until the reply: an accepted reply closes it, a failed one
 * shows the server's error.
 */
struct AddStaticDatasetDialog : public vsr::ui::imgui::Modal
{
  AddStaticDatasetDialog(
      vsr::ui::imgui::Application *app, EditorContext *context);
  ~AddStaticDatasetDialog() override;

 private:
  void buildUI() override;
  void submit();
  void reset();

  EditorContext *m_context{nullptr};
  std::string m_name;
  std::string m_sourcePath;
  int m_selectedSource{0};
  std::string m_error;
  InFlight m_pending;
  RemoteBrowseDialog m_browse;
};

} // namespace vsr::scivis_studio::client
