// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RemoteProjectActions.h"
// vsr_scivis_studio_model
#include "Project.h"

namespace vsr::scivis_studio::client {

using namespace protocol;

// RemoteStaticDatasetAction //////////////////////////////////////////////////

RemoteStaticDatasetAction::~RemoteStaticDatasetAction() = default;

void RemoteStaticDatasetAction::submit(
    const modals::AddStaticDatasetDialog::Request &request,
    modals::ActionResult done)
{
  auto onReply = replyTo(std::move(done));
  ProjectOps &ops = m_context->ops();
  if (request.importer) {
    ImportStaticDataset import;
    import.name = request.name;
    import.sourcePath = request.sourcePath;
    import.importerType = *request.importer;
    m_pending.sendForResult<TaskStartedResult>(ops, std::move(import), onReply);
  } else if (request.subtree) {
    ImportSubtreeDataset import;
    import.name = request.name;
    import.sourcePath = request.sourcePath;
    m_pending.sendForResult<TaskStartedResult>(ops, std::move(import), onReply);
  } else {
    LoadDatasetArchive load;
    load.name = request.name;
    load.file = request.sourcePath;
    m_pending.sendForResult<TaskStartedResult>(ops, std::move(load), onReply);
  }
}

// RemoteFileAnimationAction //////////////////////////////////////////////////

RemoteFileAnimationAction::~RemoteFileAnimationAction() = default;

void RemoteFileAnimationAction::submit(
    const modals::AddFileAnimationDatasetDialog::Request &request,
    modals::ActionResult done)
{
  ImportFileAnimationDataset import;
  import.name = request.name;
  import.sourcePaths = request.sourcePaths;
  import.importerType = vsr::io::ImporterType::VOLUME_ANIMATION;
  import.setActiveShotFrameCount = true;
  m_pending.sendForResult<TaskStartedResult>(
      m_context->ops(), std::move(import), replyTo(std::move(done)));
}

// RemoteProjectLocationAction ////////////////////////////////////////////////

RemoteProjectLocationAction::RemoteProjectLocationAction(
    EditorContext *context, UIStateProvider uiState)
    : RemoteAction(context), m_uiState(std::move(uiState))
{}

RemoteProjectLocationAction::~RemoteProjectLocationAction() = default;

void RemoteProjectLocationAction::submit(
    const modals::ProjectLocationDialog::Request &request,
    modals::ActionResult done)
{
  auto onReply = replyTo(std::move(done));
  ProjectOps &ops = m_context->ops();
  if (request.mode == modals::ProjectLocationMode::OpenProject) {
    OpenProject open;
    open.directory = request.directory;
    m_pending.sendForResult<TaskStartedResult>(ops, std::move(open), onReply);
  } else {
    SaveProject save;
    save.directory = request.directory;
    save.uiState = m_uiState ? m_uiState() : nullptr;
    m_pending.sendForResult<TaskStartedResult>(ops, std::move(save), onReply);
  }
}

std::string RemoteProjectLocationAction::initialDirectory(
    modals::ProjectLocationMode mode) const
{
  const Project *project = m_context->project();
  if (mode != modals::ProjectLocationMode::SaveProjectAs || !project)
    return {};
  return project->projectDirectory.generic_string();
}

} // namespace vsr::scivis_studio::client
