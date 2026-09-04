// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ShotOps.h"

#include "ProjectPersistence.h"

#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Camera.hpp"
#include "vsr/scene/objects/Renderer.hpp"

#include <algorithm>

namespace vsr::scivis_studio::shot {

static bool fail(const std::string &message, std::string *error)
{
  if (error)
    *error = message;
  return false;
}

vsr::scene::Object *resolveShotCamera(
    const vsr::scene::Scene &scene, Shot &shot)
{
  const auto cameraName = shot.id + "_camera";
  vsr::core::foreach_item_const(
      scene.objectDB().camera, [&](const vsr::scene::Camera *camera) {
        if (camera && camera->name() == cameraName)
          shot.camera = {ANARI_CAMERA, camera->index()};
      });

  const auto &ref = shot.camera;
  if (ref.type == ANARI_UNKNOWN || ref.objectIndex == VSR_INVALID_INDEX)
    return nullptr;
  return scene.getObject(ref.type, ref.objectIndex);
}

bool removeShot(Project &project,
    vsr::scene::Scene *scene,
    const ShotID &id,
    bool &activeChanged,
    std::string *error)
{
  activeChanged = false;
  auto itr = std::find_if(project.shots.begin(),
      project.shots.end(),
      [&](const Shot &shot) { return shot.id == id; });
  if (itr == project.shots.end())
    return fail("shot not found", error);
  if (project.shots.size() == 1)
    return fail("cannot remove the last shot", error);

  if (scene) {
    if (auto *camera = resolveShotCamera(*scene, *itr))
      scene->removeObject(camera);
    if (auto *layer = scene->layer("studio")) {
      auto shotsRoot = findDirectChild(layer->root(), "shots");
      if (auto shotNode = findDirectChild(shotsRoot, id))
        scene->removeNode(shotNode, true);
    }
  }

  activeChanged = project.activeShotId == id;
  project.shots.erase(itr);
  if (activeChanged)
    project.activeShotId = project.shots.front().id;
  return true;
}

bool updateShot(Project &project,
    const vsr::scene::Scene *scene,
    const Shot &incoming,
    std::string *error)
{
  auto *existing = project::findShot(project, incoming.id);
  if (!existing)
    return fail("shot not found", error);
  if (!incoming.lightRigId.empty()
      && !light_rig::findLightRig(project, incoming.lightRigId))
    return fail("light rig not found", error);
  if (!incoming.cameraRigId.empty()
      && !camera_rig::findCameraRig(project, incoming.cameraRigId))
    return fail("camera rig not found", error);

  const auto &settings = incoming.renderSettings;
  if (scene && settings.rendererObjectIndex != VSR_INVALID_INDEX) {
    auto renderer =
        scene->getObject<vsr::scene::Renderer>(settings.rendererObjectIndex);
    if (!renderer || renderer->rendererDeviceName() != settings.rendererLibrary)
      return fail("renderer " + std::to_string(settings.rendererObjectIndex)
              + " does not belong to ANARI library '" + settings.rendererLibrary
              + "'",
          error);
  }

  Shot shot = incoming;
  shot.camera = existing->camera;
  shot.playing = existing->playing;
  // Time in Motion is the manager's: an edit landing while the shot plays
  // (a loop or fps change from a transport) must not seek it back to the
  // frame the editor last saw.
  if (existing->playing)
    shot.currentFrame = existing->currentFrame;
  clampToValidRanges(shot);
  shot.datasetBindings.erase(std::remove_if(shot.datasetBindings.begin(),
                                 shot.datasetBindings.end(),
                                 [&](const DatasetBinding &binding) {
                                   return !project::findDataset(
                                       project, binding.datasetId);
                                 }),
      shot.datasetBindings.end());

  *existing = std::move(shot);
  return true;
}

} // namespace vsr::scivis_studio::shot
