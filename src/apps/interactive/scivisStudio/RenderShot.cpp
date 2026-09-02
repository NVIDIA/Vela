// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RenderShot.h"

#include "vsr/app/ANARIDeviceManager.h"
#include "vsr/core/Logging.hpp"
#include "vsr/rendering/index/RenderIndexAllLayers.hpp"
#include "vsr/rendering/pipeline/ImagePipeline.h"
#include "vsr/rendering/pipeline/passes/AnariSceneRenderPass.h"
#include "vsr/rendering/pipeline/passes/SaveToFilePass.h"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace vsr::scivis_studio {

namespace {

anari::Device loadFirstAvailableDevice(
    vsr::app::ANARIDeviceManager &deviceManager, std::string &libName)
{
  auto tryLoad = [&](const std::string &name) {
    return deviceManager.loadDevice(name);
  };

  if (auto device = tryLoad(libName))
    return device;

  if (!libName.empty() && libName != "{none}") {
    vsr::core::logWarning(
        "[SciVisStudio] Failed to load ANARI device '%s'; falling back to a "
        "default device",
        libName.c_str());
  }

  for (const auto &fallback : deviceManager.libraryList()) {
    if (fallback == libName)
      continue;
    if (auto device = tryLoad(fallback)) {
      libName = fallback;
      return device;
    }
  }

  libName.clear();
  return nullptr;
}

// Logs `error` and returns it in `out`; every early exit of the render.
bool failRender(RenderShotResult &out, std::string error)
{
  vsr::core::logError("[SciVisStudio] %s", error.c_str());
  out.error = std::move(error);
  return false;
}

} // namespace

bool makeShotDatasetsResident(ProjectContext &projectContext,
    const Shot &shot,
    ShotDatasetResidencyRestore &restore,
    std::string *error)
{
  auto &project = projectContext.project();
  restore.loadedForRender.clear();
  restore.projectWasDirty = project.dirty;

  for (const auto &binding : shot.datasetBindings) {
    if (!binding.enabled)
      continue;
    auto *dataset = project::findDataset(project, binding.datasetId);
    if (!dataset) {
      restoreShotDatasetResidency(projectContext, restore);
      if (error) {
        *error = "enabled shot binding references a missing dataset: "
            + binding.datasetId;
      }
      return false;
    }
    if (dataset->residency == DatasetResidency::Unloaded) {
      std::string loadError;
      if (!projectContext.loadDataset(dataset->id, &loadError)) {
        restoreShotDatasetResidency(projectContext, restore);
        if (error)
          *error = loadError;
        return false;
      }
      restore.loadedForRender.push_back(dataset->id);
    }
    if (dataset->status != DatasetStatus::Available) {
      restoreShotDatasetResidency(projectContext, restore);
      if (error) {
        *error = "dataset '" + dataset->name
            + "' is unavailable and cannot be rendered";
      }
      return false;
    }
  }
  return true;
}

void restoreShotDatasetResidency(
    ProjectContext &projectContext, const ShotDatasetResidencyRestore &restore)
{
  bool restored = true;
  for (const auto &id : restore.loadedForRender) {
    std::string error;
    if (!projectContext.unloadDataset(id, &error)) {
      vsr::core::logWarning(
          "[SciVisStudio] Failed to restore residency of dataset '%s' after "
          "rendering: %s",
          id.c_str(),
          error.c_str());
      restored = false;
    }
  }
  // Only a complete restore returns the project to its pre-render state; a
  // partial one leaves residency diverged from the manifest and must stay
  // dirty so the divergence remains visible and saveable.
  if (restored)
    projectContext.project().dirty = restore.projectWasDirty;
}

bool renderActiveShotToFrames(ProjectContext &projectContext,
    RenderShotProgress *progress,
    RenderShotResult *result)
{
  RenderShotResult local;
  RenderShotResult &out = result ? *result : local;
  out = RenderShotResult{};

  auto *ctx = projectContext.appContext();
  auto *shot = project::activeShot(projectContext.project());
  if (!ctx || !shot)
    return failRender(out, "No active shot to render");

  if (!projectContext.project().isSaved())
    return failRender(out, "Cannot render an unsaved project");

  auto *cameraObject = projectContext.resolveShotCamera(*shot);
  if (!cameraObject || cameraObject->type() != ANARI_CAMERA)
    return failRender(out, "Active shot camera is missing");

  // Final renders materialize shot intent: every bound, enabled dataset is
  // made fully resident regardless of stored residency, and a dataset that
  // cannot be made resident is a hard error before any frame is rendered.
  ShotDatasetResidencyRestore residencyRestore;
  {
    std::string residencyError;
    if (!makeShotDatasetsResident(
            projectContext, *shot, residencyRestore, &residencyError)) {
      return failRender(out, residencyError);
    }
  }
  struct ResidencyGuard
  {
    ProjectContext &projectContext;
    const ShotDatasetResidencyRestore &restore;
    ~ResidencyGuard()
    {
      restoreShotDatasetResidency(projectContext, restore);
    }
  } residencyGuard{projectContext, residencyRestore};

  const auto outputDirectory =
      projectContext.project().projectDirectory / "renders" / shot->id;
  out.outputDirectory = outputDirectory;
  std::error_code ec;
  std::filesystem::create_directories(outputDirectory, ec);
  if (ec) {
    return failRender(out,
        "Failed to create render directory '" + outputDirectory.string() + "'");
  }

  auto libName = shot->renderSettings.rendererLibrary;
  auto device = loadFirstAvailableDevice(ctx->anari, libName);
  if (!device)
    return failRender(out, "Failed to load an ANARI device for shot rendering");

  projectContext.applyActiveShot();

  auto *renderIndex = ctx->vsr.scene.updateDelegate()
                          .emplace<vsr::rendering::RenderIndexAllLayers>(
                              ctx->vsr.scene, libName, device);
  // However the render ends -- the last frame, a cancel, a refused renderer,
  // a throw from a frame's load or encode -- the scene stops mirroring into
  // the render's index and the device loses the retain taken for it.
  struct RenderIndexGuard
  {
    vsr::app::Context &ctx;
    vsr::rendering::RenderIndexAllLayers *index;
    anari::Device device;
    ~RenderIndexGuard()
    {
      ctx.vsr.scene.updateDelegate().erase(index);
      anari::release(device, device);
    }
  } renderIndexGuard{*ctx, renderIndex, device};
  renderIndex->populate();

  const auto rendererIndex = shot->renderSettings.rendererObjectIndex;
  auto rendererObject = ctx->vsr.scene.getObject(ANARI_RENDERER, rendererIndex);
  if (!rendererObject || rendererObject->rendererDeviceName() != libName) {
    return failRender(out,
        "Renderer object index " + std::to_string(rendererIndex)
            + " is unavailable for ANARI device '" + libName + "'");
  }

  auto renderer = renderIndex->renderer(rendererIndex);
  if (!renderer) {
    return failRender(out,
        "Failed to resolve renderer object index "
            + std::to_string(rendererIndex));
  }

  vsr::rendering::ImagePipeline pipeline;
  pipeline.setDimensions(
      shot->renderSettings.width, shot->renderSettings.height);
  auto *anariPass =
      pipeline.emplace_back<vsr::rendering::AnariSceneRenderPass>(device);
  anariPass->setRunAsync(false);
  anariPass->setColorFormat(ANARI_UFIXED8_RGBA_SRGB);
  anariPass->setWorld(renderIndex->world());
  anariPass->setRenderer(renderer);
  anariPass->setCamera(renderIndex->camera(shot->camera.objectIndex));

  auto *savePass = pipeline.emplace_back<vsr::rendering::SaveToFilePass>();
  savePass->setSingleShotMode(false);

  if (auto camera = renderIndex->camera(shot->camera.objectIndex)) {
    anari::setParameter(device,
        camera,
        "aspect",
        static_cast<float>(shot->renderSettings.width)
            / static_cast<float>(shot->renderSettings.height));
    anari::commitParameters(device, camera);
  }

  const int savedFrame = shot->currentFrame;
  const bool savedPlaying = shot->playing;
  const int totalFrames = std::max(1, shot->frameCount);
  const auto prefix = shot->renderSettings.outputFilePrefix.empty()
      ? shot->id
      : shot->renderSettings.outputFilePrefix;
  shot->playing = false;
  projectContext.syncAnimationManagerToActiveShot();
  // The shot's time and playback state come back whatever ends the frame
  // loop, and the interactive pipeline follows the shot again; declared
  // after the index guard so this runs first, while the index still stands.
  struct ShotStateGuard
  {
    ProjectContext &projectContext;
    Shot *shot;
    int frame;
    bool playing;
    ~ShotStateGuard()
    {
      shot->currentFrame = frame;
      shot->playing = playing;
      try {
        projectContext.syncAnimationManagerToActiveShot();
        projectContext.applyActiveShot();
      } catch (const std::exception &e) {
        vsr::core::logWarning(
            "[SciVisStudio] Failed to restore the shot after rendering: %s",
            e.what());
      }
    }
  } shotStateGuard{projectContext, shot, savedFrame, savedPlaying};

  vsr::core::logStatus("[SciVisStudio] Rendering %d frames to '%s'",
      totalFrames,
      outputDirectory.string().c_str());

  out.completed = true;
  for (int frame = 0; frame < totalFrames; ++frame) {
    if (progress && progress->onFrame
        && !progress->onFrame(frame, totalFrames)) {
      vsr::core::logStatus(
          "[SciVisStudio] Shot render canceled before frame %d/%d",
          frame,
          totalFrames);
      out.completed = false;
      out.cancelled = true;
      break;
    }

    ctx->vsr.animationMgr.setAnimationFrame(frame);

    std::ostringstream ss;
    ss << prefix << '_' << std::setfill('0') << std::setw(4) << frame << ".png";
    savePass->setFilename((outputDirectory / ss.str()).string());

    for (uint32_t sample = 0; sample < shot->renderSettings.samples; ++sample) {
      savePass->setEnabled(sample + 1 == shot->renderSettings.samples);
      pipeline.render();
    }
    ++out.framesCompleted;
  }

  return out.completed;
}

} // namespace vsr::scivis_studio
