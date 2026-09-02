// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_scivis_studio_client
#include "EditorContext.h"
#include "RemoteBrowseDialog.h"
// vsr_scivis_studio_protocol
#include "PayloadCommon.h"
// vsr_ui_imgui
#include "vsr/ui/imgui/modals/Modal.h"
// std
#include <functional>
#include <string>

namespace vsr::scivis_studio::client {

enum class ProjectLocationMode
{
  OpenProject,
  SaveProjectAs
};

/*
 * Where to open a project from or save it to: a free-text server directory
 * plus Browse (Remote Browse in directory mode, project directories marked).
 * The monolith validated the directory locally; here the dialog sends
 * OpenProject/SaveProject itself and stays open showing the server's error
 * when the reply fails, closing only on an accepted reply. Save As attaches
 * the UI state the provider builds (windows, layout, settings).
 *
 * Example:
 *   m_locationDialog->configure(ProjectLocationMode::OpenProject);
 *   m_locationDialog->show();
 *   ...
 *   if (m_locationDialog->visible()) m_locationDialog->renderUI();
 */
struct ProjectLocationDialog : public vsr::ui::imgui::Modal
{
  using UIStateProvider = std::function<protocol::SubtreePtr()>;

  ProjectLocationDialog(vsr::ui::imgui::Application *app,
      EditorContext *context,
      UIStateProvider uiState);
  ~ProjectLocationDialog() override;

  void configure(ProjectLocationMode mode);

 private:
  void buildUI() override;
  void submit();

  EditorContext *m_context{nullptr};
  UIStateProvider m_uiState;
  ProjectLocationMode m_mode{ProjectLocationMode::OpenProject};
  std::string m_directory;
  std::string m_error;
  RequestHandle m_pending;
  RemoteBrowseDialog m_browse;
};

} // namespace vsr::scivis_studio::client
