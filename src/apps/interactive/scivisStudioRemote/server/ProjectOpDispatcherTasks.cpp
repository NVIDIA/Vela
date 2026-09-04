// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// The task-launching half of ProjectOpDispatcher: the handlers that answer
// with a TaskStartedResult and queue their body on the ServerTaskRunner, and
// startTask()/runTaskBody() they share. The sync ops, Remote Browse,
// CancelTask and the request policy table are in ProjectOpDispatcher.cpp.

#include "ProjectOpDispatcher.h"
// vsr_scivis_studio_protocol
#include "ProjectSnapshot.h" // UIState
#include "StudioCodec.h"
// vsr_scivis_studio_model
#include "ProjectPersistence.h"
#include "RenderShot.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// std
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsr::scivis_studio::server {

using namespace protocol;

namespace {

// The failure of a task body whose dataset is gone by the time it runs.
TaskResult datasetNotFound()
{
  TaskResult result;
  result.ok = false;
  result.error = "dataset not found";
  return result;
}

// What an import task reports for the dataset the context handed back: null
// is a refusal; a record that is not Available is a failed import (the
// ImportFailed record stays in the project, so a snapshot still follows).
TaskResult importResult(const Dataset *dataset)
{
  TaskResult result;
  if (!dataset) {
    result.ok = false;
    result.error = "import failed";
    return result;
  }
  result.message = dataset->id;
  if (dataset->status != DatasetStatus::Available) {
    result.ok = false;
    result.error = "dataset '" + dataset->name + "' import failed ("
        + dataset::displayStatus(*dataset) + "); see the server log";
  }
  return result;
}

} // namespace

// Task plumbing ///////////////////////////////////////////////////////////////

void ProjectOpDispatcher::startTask(
    uint64_t requestId, std::string description, TaskBody body, bool exclusive)
{
  const auto taskId =
      m_host.tasks->enqueue(std::move(description), std::move(body), exclusive);
  auto reply = makeOkReply(requestId);
  setResults(reply, TaskStartedResult{taskId});
  finish(reply);
}

TaskResult ProjectOpDispatcher::runTaskBody(
    const std::function<TaskResult()> &body)
{
  // The flush follows the body whatever its outcome: a failed (or throwing)
  // open may have reset the scene already, and the TransferScene it asked
  // for must reach the client before the ending.
  TaskResult result;
  try {
    result = body();
  } catch (...) {
    m_host.flushScenePushes();
    throw;
  }
  m_host.flushScenePushes();
  return result;
}

// Project /////////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const OpenProject &req)
{
  std::string error;
  const auto directory = m_host.dataRoots->resolve(req.directory, &error);
  if (!directory) {
    fail(req.requestId, error);
    return;
  }

  startTask(req.requestId,
      "open project '" + directory->string() + "'",
      [this, directory = *directory](const TaskControl &progress) {
        return runTaskBody([&] {
          TaskResult result;
          progress("staging");
          ProjectOpenStage stage;
          if (!stageProjectOpen(directory, stage, {}, &result.error)) {
            result.ok = false;
            return result;
          }
          progress("applying");
          auto uiState = makeSubtree();
          if (!context().openStagedProject(
                  stage, &uiState->root(), &result.error)) {
            result.ok = false;
            return result;
          }
          *m_host.uiState = uiState;
          // The opened project's layout reaches the client that asked
          // before the snapshot that follows the task; a bootstrap
          // carries it too.
          m_host.send(encode(UIState{*m_host.uiState}));
          return result;
        });
      });
}

void ProjectOpDispatcher::handle(const SaveProject &req)
{
  // Only a named directory can be checked now; "the project's own" is read
  // when the task runs, since an OpenProject queued ahead may change it.
  std::optional<std::filesystem::path> named;
  if (req.directory) {
    std::string error;
    named = m_host.dataRoots->resolve(*req.directory, &error);
    if (!named) {
      fail(req.requestId, error);
      return;
    }
  }

  startTask(req.requestId,
      named ? "save project to '" + named->string() + "'"
            : std::string("save project"),
      [this, named, uiState = req.uiState](const TaskControl &progress) {
        return runTaskBody([&] {
          TaskResult result;
          auto directory = named;
          if (!directory) {
            const auto &own = project().projectDirectory;
            if (own.empty()) {
              result.ok = false;
              result.error = "project has never been saved; choose a directory";
              return result;
            }
            directory = m_host.dataRoots->resolve(own, &result.error);
            if (!directory) {
              result.ok = false;
              return result;
            }
          }
          progress("writing");
          // A save without UI state keeps what the project opened with,
          // so a headless save never drops the user's layout.
          const auto &tree = uiState ? uiState : *m_host.uiState;
          if (!context().saveProject(
                  *directory, tree ? &tree->root() : nullptr, &result.error)) {
            result.ok = false;
            return result;
          }
          if (uiState)
            *m_host.uiState = uiState;
          return result;
        });
      });
}

// Datasets ////////////////////////////////////////////////////////////////////

void ProjectOpDispatcher::handle(const ImportStaticDataset &req)
{
  std::string error;
  const auto source = m_host.dataRoots->resolve(req.sourcePath, &error);
  if (!source) {
    fail(req.requestId, error);
    return;
  }

  startTask(req.requestId,
      "import '" + source->string() + "'",
      [this, req, source = *source](const TaskControl &progress) {
        return runTaskBody([&] {
          progress("importing");
          return importResult(
              context().addStaticDataset(req.name, source, req.importerType));
        });
      });
}

void ProjectOpDispatcher::handle(const ImportSubtreeDataset &req)
{
  std::string error;
  const auto source = m_host.dataRoots->resolve(req.sourcePath, &error);
  if (!source) {
    fail(req.requestId, error);
    return;
  }

  startTask(req.requestId,
      "import subtree '" + source->string() + "'",
      [this, req, source = *source](const TaskControl &progress) {
        return runTaskBody([&] {
          progress("importing");
          return importResult(
              context().addStaticDatasetFromSubtree(req.name, source));
        });
      });
}

void ProjectOpDispatcher::handle(const ImportFileAnimationDataset &req)
{
  if (req.sourcePaths.empty()) {
    fail(req.requestId, "no source paths given");
    return;
  }
  std::vector<std::filesystem::path> sources;
  for (const auto &path : req.sourcePaths) {
    std::string error;
    const auto source = m_host.dataRoots->resolve(path, &error);
    if (!source) {
      fail(req.requestId, error);
      return;
    }
    sources.push_back(*source);
  }

  startTask(req.requestId,
      "import " + std::to_string(sources.size()) + " animation frame(s) from '"
          + sources.front().string() + "'",
      [this, req, sources = std::move(sources)](const TaskControl &progress) {
        return runTaskBody([&] {
          progress("importing");
          FileAnimationDatasetOptions options;
          options.setActiveShotFrameCount = req.setActiveShotFrameCount;
          return importResult(context().addFileAnimationDataset(
              req.name, sources, req.importerType, options));
        });
      });
}

void ProjectOpDispatcher::handle(const ReimportDataset &req)
{
  startTask(req.requestId,
      "reimport dataset '" + req.datasetId + "'",
      [this, id = req.datasetId](const TaskControl &progress) {
        return runTaskBody([&] {
          TaskResult result;
          if (!project::findDataset(project(), id))
            return datasetNotFound();
          progress("reimporting");
          result.ok = context().reimportStaticDataset(id, &result.error);
          return result;
        });
      });
}

void ProjectOpDispatcher::handle(const LoadDataset &req)
{
  startTask(req.requestId,
      "load dataset '" + req.datasetId + "'",
      [this, id = req.datasetId](const TaskControl &progress) {
        return runTaskBody([&] {
          TaskResult result;
          if (!project::findDataset(project(), id))
            return datasetNotFound();
          progress("loading");
          result.ok = context().loadDataset(id, &result.error);
          return result;
        });
      });
}

void ProjectOpDispatcher::handle(const SaveDatasetArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  startTask(req.requestId,
      "save dataset '" + req.datasetId + "' to '" + file->string() + "'",
      [this, id = req.datasetId, file = *file](const TaskControl &progress) {
        return runTaskBody([&] {
          TaskResult result;
          if (!project::findDataset(project(), id))
            return datasetNotFound();
          progress("writing");
          result.ok = context().saveDatasetArchive(id, file, &result.error);
          return result;
        });
      });
}

void ProjectOpDispatcher::handle(const LoadDatasetArchive &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  startTask(req.requestId,
      "load dataset archive '" + file->string() + "'",
      [this, file = *file](const TaskControl &progress) {
        return runTaskBody([&] {
          TaskResult result;
          progress("loading");
          auto *dataset = context().loadDatasetArchive(file, &result.error);
          result.ok = dataset != nullptr;
          if (dataset)
            result.message = dataset->id;
          return result;
        });
      });
}

void ProjectOpDispatcher::handle(const IncorporateDatasetCandidate &req)
{
  std::string error;
  const auto file = m_host.dataRoots->resolve(req.file, &error);
  if (!file) {
    fail(req.requestId, error);
    return;
  }
  // The candidate keeps the path as discovered: incorporate compares it with
  // the project's datasets directory to tell a managed file from a rename.
  const DatasetCandidate candidate{req.file, req.proposedName};
  startTask(req.requestId,
      "incorporate dataset candidate '" + file->string() + "'",
      [this, candidate, name = req.name](const TaskControl &progress) {
        return runTaskBody([&] {
          TaskResult result;
          progress("loading");
          auto *dataset = context().incorporateDatasetCandidate(
              candidate, name, &result.error);
          result.ok = dataset != nullptr;
          if (dataset)
            result.message = dataset->id;
          return result;
        });
      });
}

// Offline render //////////////////////////////////////////////////////////////

// The offline render. Sync prelude first (the shot becomes active; the loop
// rebinds and snapshots on it), then the exclusive task whose body walks
// the frames, reporting each and stopping at the next boundary
// once a cancel or the shutdown is asked for. Partial frames stay on disk;
// the ending carries how many were written. dispatch() has already refused
// this request when a render is pending, and the host held it back until
// the tasks sent before it had run, so the prelude reads the Project those
// left.
void ProjectOpDispatcher::handle(const RenderShot &req)
{
  if (!project::findShot(project(), req.shotId)) {
    fail(req.requestId, "shot not found");
    return;
  }
  if (!project().isSaved()) {
    fail(req.requestId, "project is not saved; save it before rendering");
    return;
  }
  if (project().activeShotId != req.shotId) {
    std::string error;
    if (!context().setActiveShot(req.shotId, &error)) {
      fail(req.requestId, error);
      return;
    }
  }

  startTask(
      req.requestId,
      "render shot '" + req.shotId + "'",
      [this, shotId = req.shotId](const TaskControl &task) {
        return runTaskBody([&] {
          TaskResult result;
          // Mutations are refused while the render is pending, so the
          // shot made active by the prelude is still the one; a body
          // that found otherwise would render the wrong shot.
          if (project().activeShotId != shotId) {
            result.ok = false;
            result.error = "shot '" + shotId + "' is no longer active";
            return result;
          }
          // The latch must be discarded before the task's ending goes
          // out, and a frame's load or encode can throw out of
          // renderActiveShotToFrames: the destructor covers the throw
          // (which runTaskBody rethrows to the runner) and the return.
          struct LatchGuard
          {
            const std::function<void()> &drop;
            ~LatchGuard()
            {
              if (drop)
                drop();
            }
          } latchGuard{m_host.dropLatchedInputs};

          RenderShotProgress progress;
          progress.onFrame = [&](int frame, int totalFrames) {
            if (task.cancelRequested())
              return false;
            if (m_host.shutdownRequested && m_host.shutdownRequested()) {
              vsr::core::logStatus(
                  "[StudioServer] shot render stopped: server shutting"
                  " down");
              return false;
            }
            task(uint64_t(frame) + 1,
                uint64_t(totalFrames),
                "frame " + std::to_string(frame + 1) + " of "
                    + std::to_string(totalFrames));
            return true;
          };
          const auto rendered = renderActiveShotToFrames(context(), &progress);
          result.ok = rendered.completed;
          result.error = rendered.cancelled ? "cancelled" : rendered.error;
          result.cancelled = rendered.cancelled;
          result.message = rendered.outputDirectory.string();
          setResults(result,
              protocol::RenderShotResult{uint64_t(rendered.framesCompleted)});
          return result;
        });
      },
      /*exclusive=*/true);
}

} // namespace vsr::scivis_studio::server
