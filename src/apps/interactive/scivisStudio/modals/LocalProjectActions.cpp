// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LocalProjectActions.h"
// scivisStudio
#include "ProjectSerialization.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_ui_imgui
#include "vsr/ui/imgui/Application.h"
// std
#include <cstddef>
#include <system_error>
#include <vector>

namespace vsr::scivis_studio {

namespace {

// The frames the project would fail to read, by index, so the dialog can
// mark those rows. Only the monolith can ask: the client's frames are the
// server's files.
std::vector<size_t> missingFrames(
    const std::vector<std::filesystem::path> &sourcePaths)
{
  std::vector<size_t> missing;
  for (size_t i = 0; i < sourcePaths.size(); ++i) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(sourcePaths[i], ec) && !ec)
      continue;
    missing.push_back(i);
  }
  return missing;
}

} // namespace

// LocalStaticDatasetAction ///////////////////////////////////////////////////

LocalStaticDatasetAction::LocalStaticDatasetAction(
    vsr::ui::imgui::Application *app, ProjectContext *projectContext)
    : m_app(app), m_projectContext(projectContext)
{}

LocalStaticDatasetAction::~LocalStaticDatasetAction() = default;

void LocalStaticDatasetAction::submit(
    const modals::AddStaticDatasetDialog::Request &request,
    modals::ActionResult done)
{
  const char *progressLabel = request.importer
      ? "Importing Dataset..."
      : (request.subtree ? "Loading Layer Subtree Archive..."
                         : "Loading Dataset Archive...");

  m_app->showTaskModal(
      [ctx = m_projectContext, request]() {
        if (!ctx)
          return;
        if (request.importer) {
          ctx->addStaticDataset(
              request.name, request.sourcePath, *request.importer);
        } else if (request.subtree) {
          auto *dataset = ctx->addStaticDatasetFromSubtree(
              request.name, request.sourcePath);
          if (!dataset || dataset->status != DatasetStatus::Available) {
            vsr::core::logWarning(
                "[SciVisStudio] Failed to load Layer Subtree Archive as a dataset");
          }
        } else {
          std::string error;
          auto *dataset =
              ctx->loadDatasetArchive(request.sourcePath, {}, &error);
          if (!dataset) {
            vsr::core::logWarning(
                "[SciVisStudio] Failed to load Dataset Archive: %s",
                error.c_str());
          } else if (!request.name.empty()
              && !ctx->renameDataset(dataset->id, request.name, &error)) {
            vsr::core::logWarning(
                "[SciVisStudio] Failed to rename loaded Dataset Archive: %s",
                error.c_str());
          }
        }
      },
      progressLabel);

  // The task modal owns the rest: whatever it fails at is logged, not shown
  // back in a dialog that is already gone.
  done(true, {});
}

// LocalFileAnimationAction ///////////////////////////////////////////////////

LocalFileAnimationAction::LocalFileAnimationAction(
    vsr::ui::imgui::Application *app, ProjectContext *projectContext)
    : m_app(app), m_projectContext(projectContext)
{}

LocalFileAnimationAction::~LocalFileAnimationAction() = default;

std::vector<size_t> LocalFileAnimationAction::unreadableFrames(
    const modals::AddFileAnimationDatasetDialog::Request &request) const
{
  return missingFrames(request.sourcePaths);
}

void LocalFileAnimationAction::submit(
    const modals::AddFileAnimationDatasetDialog::Request &request,
    modals::ActionResult done)
{
  m_app->showTaskModal(
      [ctx = m_projectContext, request]() {
        if (ctx) {
          ctx->addFileAnimationDataset(request.name,
              request.sourcePaths,
              vsr::io::ImporterType::VOLUME_ANIMATION);
        }
      },
      "Importing File Animation Dataset...");
  done(true, {});
}

// LocalProjectLocationAction /////////////////////////////////////////////////

LocalProjectLocationAction::LocalProjectLocationAction(Accept accept)
    : m_accept(std::move(accept))
{}

LocalProjectLocationAction::~LocalProjectLocationAction() = default;

void LocalProjectLocationAction::submit(
    const modals::ProjectLocationDialog::Request &request,
    modals::ActionResult done)
{
  const auto &directory = request.directory;
  if (request.mode == modals::ProjectLocationMode::OpenProject) {
    auto result = validateProjectRoot(directory);
    if (!result.ok) {
      done(false, result.error);
      return;
    }
  } else {
    const auto manifest =
        resolveProjectFileForRead(directory / PROJECT_MANIFEST_FILENAME);
    if (std::filesystem::exists(manifest)) {
      done(false,
          "Target directory already contains " + manifest.filename().string()
              + ".");
      return;
    }
    if (std::filesystem::exists(directory)
        && !std::filesystem::is_directory(directory)) {
      done(false, "Target path is not a directory.");
      return;
    }
  }

  done(true, {});
  if (m_accept)
    m_accept(request.mode, directory);
}

std::string LocalProjectLocationAction::initialDirectory(
    modals::ProjectLocationMode) const
{
  return {};
}

} // namespace vsr::scivis_studio
