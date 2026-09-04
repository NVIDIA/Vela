// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectPersistence.h"

#include "CameraRig.h"
#include "DatasetIO.h"
#include "LightRigIO.h"
#include "ProjectSerialization.h"

#include "vsr/animation/AnimationManager.hpp"
#include "vsr/core/DataTreeMetadata.hpp"
#include "vsr/core/Logging.hpp"
#include "vsr/io/archives/CameraArchive.hpp"
#include "vsr/io/archives/RendererArchive.hpp"
#include "vsr/io/serialization/serialization_internal.hpp"
#include "vsr/scene/Scene.hpp"
#include "vsr/scene/objects/Camera.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

namespace vsr::scivis_studio {

namespace {

struct StagedArchive
{
  std::shared_ptr<vsr::core::DataTree> tree;
  std::string error;
};

struct StagedCameraRig
{
  bool loaded{false};
  CameraRig rig;
  std::string error;
};

/*
 * A staged dataset is the archive plus — for a new-format file-animation
 * dataset — its sibling Source List File, read once while staging so the
 * stage-and-validate open works from one consistent snapshot (ADR 0013).
 */
struct StagedDataset
{
  StagedArchive archive;
  bool sourceListLoaded{false};
  std::vector<DatasetSourceFile> sourceList;
  std::string sourceListError;
};

bool fail(std::string message, std::string *error)
{
  if (error)
    *error = std::move(message);
  return false;
}

StagedArchive stageArchive(const std::filesystem::path &file)
{
  StagedArchive staged;
  staged.tree = std::make_shared<vsr::core::DataTree>();
  if (!staged.tree->load(file.string().c_str())) {
    staged.tree.reset();
    staged.error = "Archive is missing or unreadable";
  }
  return staged;
}

vsr::scene::LayerNodeRef findDirectChild(
    vsr::scene::LayerNodeRef parent, const std::string &name)
{
  if (!parent)
    return {};
  auto child = parent->next();
  while (child && child != parent) {
    if ((*child)->name() == name)
      return child;
    child = child->sibling();
  }
  return {};
}

vsr::scene::LayerNodeRef ensureChild(
    vsr::scene::Scene &scene, vsr::scene::LayerNodeRef parent, const char *name)
{
  if (auto found = findDirectChild(parent, name))
    return found;
  return scene.insertChildNode(parent, name);
}

vsr::scene::LayerNodeRef ensureStudioRoot(vsr::scene::Scene &scene)
{
  auto *layer = scene.addLayer("studio");
  return layer ? layer->root() : vsr::scene::LayerNodeRef{};
}

vsr::scene::LayerNodeRef ensureCollectionRoot(
    vsr::scene::Scene &scene, const char *name)
{
  return ensureChild(scene, ensureStudioRoot(scene), name);
}

SceneNodeRef nodeRef(const char *layerName, vsr::scene::LayerNodeRef node)
{
  return {layerName, node ? node.index() : VSR_INVALID_INDEX};
}

vsr::scene::LayerNodeRef resolveNode(
    vsr::scene::Scene &scene, const SceneNodeRef &ref)
{
  if (ref.layerName.empty() || ref.nodeIndex == VSR_INVALID_INDEX)
    return {};
  auto *layer = scene.layer(ref.layerName.c_str());
  return layer ? layer->at(ref.nodeIndex) : vsr::scene::LayerNodeRef{};
}

vsr::scene::LayerNodeRef resolveAssetRoot(vsr::scene::Scene &scene,
    const char *collection,
    const std::string &id,
    SceneNodeRef &fallback)
{
  if (auto *layer = scene.layer("studio")) {
    auto collectionRoot = findDirectChild(layer->root(), collection);
    if (auto assetRoot = findDirectChild(collectionRoot, id)) {
      fallback = nodeRef("studio", assetRoot);
      return assetRoot;
    }
  }
  return resolveNode(scene, fallback);
}

void resetScene(vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animationManager)
{
  animationManager.removeAllAnimations();
  scene.removeAllObjects();
  scene.defaultMaterial();
  scene.defaultCamera();
}

void clearCameraRigBindings(Project &project, const CameraRigID &cameraRigId)
{
  for (auto &shot : project.shots) {
    if (shot.cameraRigId == cameraRigId)
      shot.cameraRigId.clear();
  }
}

void clearLightRigBindings(Project &project, const LightRigID &lightRigId)
{
  for (auto &shot : project.shots) {
    if (shot.lightRigId == lightRigId)
      shot.lightRigId.clear();
  }
}

void migrateLegacyShotLights(Project &project, vsr::scene::Scene &scene)
{
  if (!project.lightRigs.empty())
    return;
  auto *layer = scene.layer("studio");
  if (!layer)
    return;

  auto lightRigsRoot = ensureCollectionRoot(scene, "lightRigs");
  auto shotsRoot = findDirectChild(layer->root(), "shots");
  for (auto &shot : project.shots) {
    auto shotRoot = findDirectChild(shotsRoot, shot.id);
    auto legacyLights = findDirectChild(shotRoot, "lights");
    if (!legacyLights)
      continue;

    LightRig rig;
    rig.id = light_rig::nextLightRigId(project);
    rig.name =
        shot.name.empty() ? (shot.id + " Lights") : (shot.name + " Lights");
    if (auto existing = findDirectChild(lightRigsRoot, rig.id))
      scene.removeNode(existing, true);
    legacyLights->container()->move_subtree(legacyLights, lightRigsRoot);
    (*legacyLights)->name() = rig.id;
    rig.rootNode = nodeRef("studio", legacyLights);
    shot.lightRigId = rig.id;
    project.lightRigs.push_back(std::move(rig));
  }
  scene.signalLayerStructureChanged(layer);
}

void hydrateCameraRigs(
    const detail::ProjectOpenState &state, Project &project, bool logWarnings);
void hydrateLightRigs(const detail::ProjectOpenState &state,
    Project &project,
    vsr::scene::Scene &scene,
    bool logWarnings);
void hydrateDatasets(const detail::ProjectOpenState &state,
    Project &project,
    vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animationManager,
    bool logWarnings);

void refreshRuntimeRefs(Project &project, vsr::scene::Scene &scene)
{
  for (auto &dataset : project.datasets)
    resolveAssetRoot(scene, "datasets", dataset.id, dataset.rootNode);
  for (auto &rig : project.lightRigs)
    resolveAssetRoot(scene, "lightRigs", rig.id, rig.rootNode);

  for (auto &shot : project.shots) {
    const auto cameraName = shot.id + "_camera";
    const auto &cameras = scene.objectDB().camera;
    vsr::core::foreach_item_const(
        cameras, [&](const vsr::scene::Camera *camera) {
          if (camera && camera->name() == cameraName)
            shot.camera = {ANARI_CAMERA, camera->index()};
        });
  }
}

bool reconstructProject(const detail::ProjectOpenState &state,
    Project &project,
    vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animationManager,
    bool logWarnings,
    std::string *error);

} // namespace

namespace detail {

struct ProjectOpenState
{
  std::filesystem::path directory;
  std::shared_ptr<vsr::core::DataTree> manifest;
  Project manifestProject;
  int schemaVersion{0};
  // An open-time residency override diverged from the manifest, so the
  // project must open dirty.
  bool residencyOverrideDirtied{false};
  // Bookkeeping mode: no dataset runtime is built and recorded residency is
  // round-tripped unchanged.
  bool bookkeeping{false};
  StagedArchive cameras;
  StagedArchive renderers;
  std::vector<StagedCameraRig> cameraRigs;
  std::vector<StagedArchive> lightRigs;
  std::vector<StagedDataset> datasets;
};

} // namespace detail

namespace {

void hydrateCameraRigs(
    const detail::ProjectOpenState &state, Project &project, bool logWarnings)
{
  std::vector<CameraRig> kept;
  kept.reserve(project.cameraRigs.size());
  for (size_t i = 0; i < project.cameraRigs.size(); ++i) {
    auto &rig = project.cameraRigs[i];
    const auto &staged = state.cameraRigs[i];
    if (!staged.loaded) {
      if (logWarnings) {
        vsr::core::logWarning(
            "[SciVisStudio] Skipping Camera Rig Archive '%s': %s",
            rig.name.c_str(),
            staged.error.c_str());
      }
      clearCameraRigBindings(project, rig.id);
      continue;
    }
    rig.current = staged.rig.current;
    rig.keyframes = staged.rig.keyframes;
    rig.persistedName = rig.name;
    kept.push_back(std::move(rig));
  }
  project.cameraRigs = std::move(kept);
}

void hydrateLightRigs(const detail::ProjectOpenState &state,
    Project &project,
    vsr::scene::Scene &scene,
    bool logWarnings)
{
  std::vector<LightRig> kept;
  kept.reserve(project.lightRigs.size());
  for (size_t i = 0; i < project.lightRigs.size(); ++i) {
    auto &rig = project.lightRigs[i];
    const auto &staged = state.lightRigs[i];
    vsr::scene::LayerNodeRef root;
    if (staged.tree) {
      auto destination = ensureCollectionRoot(scene, "lightRigs");
      std::string displayName;
      root = deserializeLightRigArchive(
          scene, staged.tree->root(), destination, &displayName);
    }
    if (!root) {
      if (logWarnings) {
        vsr::core::logWarning(
            "[SciVisStudio] Skipping Light Rig Archive '%s': %s",
            rig.name.c_str(),
            staged.error.empty() ? "failed to load Archive"
                                 : staged.error.c_str());
      }
      clearLightRigBindings(project, rig.id);
      continue;
    }
    (*root)->name() = rig.id;
    rig.rootNode = nodeRef("studio", root);
    rig.persistedName = rig.name;
    kept.push_back(std::move(rig));
  }
  project.lightRigs = std::move(kept);
}

void hydrateDatasets(const detail::ProjectOpenState &state,
    Project &project,
    vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animationManager,
    bool logWarnings)
{
  auto destination = ensureCollectionRoot(scene, "datasets");
  for (size_t i = 0; i < project.datasets.size(); ++i) {
    auto &inventoryEntry = project.datasets[i];
    const auto &staged = state.datasets[i];

    // Opening hydrates only resident datasets. An Unloaded dataset stays out
    // of the scene; a cheap existence check reveals a definitively missing
    // asset, while the authoritative assessment remains the load attempt. A
    // bookkeeping open hydrates nothing while keeping recorded residency.
    if (inventoryEntry.residency == DatasetResidency::Unloaded
        || state.bookkeeping) {
      inventoryEntry.dirty = false;
      inventoryEntry.persistedName = inventoryEntry.name;
      std::error_code ec;
      const bool assetExists = std::filesystem::exists(
          resolveProjectFileForRead(
              state.directory / "datasets" / (inventoryEntry.name + ".vsr")),
          ec);
      inventoryEntry.status = (assetExists && !ec) ? DatasetStatus::Available
                                                   : DatasetStatus::Unavailable;
      if (inventoryEntry.status == DatasetStatus::Unavailable && logWarnings) {
        vsr::core::logWarning(
            "[SciVisStudio] Dataset '%s' has no asset on disk",
            inventoryEntry.name.c_str());
      }
      continue;
    }
    Dataset loaded;
    vsr::scene::LayerNodeRef loadedRoot;
    std::string datasetError = staged.archive.error;
    const bool loadedAsset = staged.archive.tree
        && deserializeDatasetArchive(scene,
            animationManager,
            staged.archive.tree->root(),
            destination,
            staged.sourceListLoaded ? &staged.sourceList : nullptr,
            loaded,
            loadedRoot,
            &datasetError);
    if (!loadedAsset) {
      if (!staged.sourceListError.empty())
        datasetError = staged.sourceListError;
      inventoryEntry.status = DatasetStatus::Unavailable;
      inventoryEntry.dirty = false;
      inventoryEntry.persistedName = inventoryEntry.name;
      if (logWarnings) {
        vsr::core::logWarning("[SciVisStudio] Dataset '%s' is unavailable: %s",
            inventoryEntry.name.c_str(),
            datasetError.c_str());
      }
      continue;
    }

    if (loaded.name != inventoryEntry.name) {
      removeDatasetRuntime(scene, animationManager, loadedRoot);
      inventoryEntry.status = DatasetStatus::Unavailable;
      inventoryEntry.dirty = false;
      inventoryEntry.persistedName = inventoryEntry.name;
      if (logWarnings) {
        vsr::core::logWarning(
            "[SciVisStudio] Dataset '%s' is unavailable: asset name is '%s'",
            inventoryEntry.name.c_str(),
            loaded.name.c_str());
      }
      continue;
    }

    loaded.id = inventoryEntry.id;
    loaded.name = inventoryEntry.name;
    loaded.persistedName = inventoryEntry.name;
    // loaded.dirty stays as deserialized: clean for an ordinary asset, dirty
    // when a Declared Dataset just materialized (ADR 0014).
    (*loadedRoot)->name() = loaded.id;
    loaded.rootNode = nodeRef("studio", loadedRoot);
    inventoryEntry = std::move(loaded);
  }
}

bool reconstructProject(const detail::ProjectOpenState &state,
    Project &project,
    vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animationManager,
    bool logWarnings,
    std::string *error)
{
  resetScene(scene, animationManager);
  project = state.manifestProject;

  if (state.schemaVersion >= DECOMPOSED_SCENE_SCHEMA_VERSION) {
    auto shotsRoot = ensureCollectionRoot(scene, "shots");
    ensureCollectionRoot(scene, "datasets");
    ensureCollectionRoot(scene, "lightRigs");
    for (const auto &shot : project.shots)
      ensureChild(scene, shotsRoot, shot.id.c_str());

    if (!state.cameras.tree || !state.renderers.tree
        || !vsr::io::deserialize_CameraArchive(
            scene, state.cameras.tree->root())
        || !vsr::io::deserialize_RendererArchive(
            scene, state.renderers.tree->root())) {
      return fail("failed to load required scene pool Archives", error);
    }
  } else if (auto *context = state.manifest->root().child("context")) {
    if (!vsr::io::detail::tryDeserializeLegacyScenePayload(
            scene, *context, nullptr, &animationManager)) {
      return fail("failed to load legacy project context", error);
    }
  }

  project.projectDirectory = state.directory;
  project.markClean();
  if (state.residencyOverrideDirtied)
    project.markDirty();
  if (state.schemaVersion < 2)
    migrateLegacyShotLights(project, scene);
  if (state.schemaVersion >= 4) {
    hydrateCameraRigs(state, project, logWarnings);
    hydrateLightRigs(state, project, scene, logWarnings);
  }
  if (state.schemaVersion >= 5) {
    hydrateDatasets(state, project, scene, animationManager, logWarnings);
  } else {
    for (auto &dataset : project.datasets) {
      dataset.status =
          resolveAssetRoot(scene, "datasets", dataset.id, dataset.rootNode)
          ? DatasetStatus::Available
          : DatasetStatus::Unavailable;
    }
  }
  refreshRuntimeRefs(project, scene);
  return true;
}

bool stageRequiredPoolArchive(const std::filesystem::path &file,
    StagedArchive &staged,
    vsr::io::ArchiveValidationResult (*validate)(vsr::core::DataNode &),
    std::string *error)
{
  staged = stageArchive(file);
  if (!staged.tree)
    return fail("required Archive is missing: " + file.string(), error);
  const auto validation = validate(staged.tree->root());
  if (!validation.accepted()) {
    return fail("invalid required Archive '" + file.string()
            + "': " + validation.message,
        error);
  }
  return true;
}

} // namespace

bool stageProjectOpen(const std::filesystem::path &directory,
    ProjectOpenStage &stage,
    const ProjectOpenOptions &options,
    std::string *error)
{
  stage.m_state.reset();
  stage.project = {};
  stage.ui.root().reset();

  const auto validation = validateProjectRoot(directory);
  if (!validation.ok)
    return fail(validation.error, error);

  auto state = std::make_shared<detail::ProjectOpenState>();
  state->directory = directory;
  state->manifest = std::make_shared<vsr::core::DataTree>();
  if (!state->manifest->load(validation.manifestPath.string().c_str()))
    return fail("failed to load project.vsr", error);

  auto &root = state->manifest->root();
  const auto *model = root.child("scivisStudio");
  if (!model)
    return fail("project.vsr is missing scivisStudio section", error);
  if (!nodeToProject(*model, state->manifestProject, ProjectForm::Manifest))
    return fail("project.vsr has a malformed scivisStudio section", error);

  const auto metadata = vsr::core::readDataTreeMetadata(root);
  state->schemaVersion = metadata.found()
      ? metadata.metadata->schemaVersion
      : root["schemaVersion"].getValueOr<int>(1);

  state->bookkeeping = options.bookkeeping;
  if (options.openUnloaded && !options.bookkeeping) {
    // Pre-v5 projects embed dataset payloads in the manifest and hydrate them
    // unconditionally, so an unloaded override would claim memory savings it
    // cannot deliver and mark never-persisted datasets read-only.
    if (state->schemaVersion >= 5) {
      for (auto &dataset : state->manifestProject.datasets) {
        if (dataset.residency != DatasetResidency::Unloaded) {
          dataset.residency = DatasetResidency::Unloaded;
          state->residencyOverrideDirtied = true;
        }
      }
    } else {
      vsr::core::logWarning(
          "[SciVisStudio] --openUnloaded is ignored for legacy projects "
          "(schema version %i)",
          state->schemaVersion);
    }
  }
  // Pre-v5 projects embed dataset payloads in the manifest and hydrate them
  // unconditionally, so a bookkeeping open cannot avoid building them.
  if (options.bookkeeping && state->schemaVersion < 5) {
    vsr::core::logWarning(
        "[SciVisStudio] bookkeeping open builds legacy embedded datasets "
        "(schema version %i)",
        state->schemaVersion);
  }

  if (state->schemaVersion >= DECOMPOSED_SCENE_SCHEMA_VERSION) {
    if (!stageRequiredPoolArchive(
            resolveProjectFileForRead(directory / "scene/cameras.vsr"),
            state->cameras,
            vsr::io::validate_CameraArchive,
            error)
        || !stageRequiredPoolArchive(
            resolveProjectFileForRead(directory / "scene/renderers.vsr"),
            state->renderers,
            vsr::io::validate_RendererArchive,
            error)) {
      return false;
    }
  }

  if (state->schemaVersion >= 4) {
    state->cameraRigs.reserve(state->manifestProject.cameraRigs.size());
    for (const auto &rig : state->manifestProject.cameraRigs) {
      StagedCameraRig staged;
      staged.loaded = camera_rig::loadCameraRigArchiveFile(
          resolveProjectFileForRead(
              directory / "cameras" / (rig.name + ".vsr")),
          staged.rig,
          &staged.error);
      state->cameraRigs.push_back(std::move(staged));
    }
    state->lightRigs.reserve(state->manifestProject.lightRigs.size());
    for (const auto &rig : state->manifestProject.lightRigs) {
      state->lightRigs.push_back(stageArchive(resolveProjectFileForRead(
          directory / "lights" / (rig.name + ".vsr"))));
    }
  }
  if (state->schemaVersion >= 5) {
    state->datasets.reserve(state->manifestProject.datasets.size());
    for (const auto &dataset : state->manifestProject.datasets) {
      // Non-resident datasets are not even staged into memory; keeping them
      // out of the open is the point of residency. A bookkeeping open stages
      // no dataset at all.
      if (dataset.residency == DatasetResidency::Unloaded
          || state->bookkeeping) {
        state->datasets.emplace_back();
        continue;
      }
      const auto file = resolveProjectFileForRead(
          directory / "datasets" / (dataset.name + ".vsr"));
      StagedDataset staged;
      staged.archive = stageArchive(file);
      if (staged.archive.tree
          && datasetArchiveUsesSourceListFile(staged.archive.tree->root())) {
        staged.sourceListLoaded = readSourceListFile(sourceListFilePath(file),
            staged.sourceList,
            &staged.sourceListError);
      }
      state->datasets.push_back(std::move(staged));
    }
  }

  if (auto *windows = root.child("windows"))
    stage.ui.root()["windows"] = *windows;
  if (auto *layout = root.child("layout"))
    stage.ui.root()["layout"] = layout->getValueAs<std::string>();
  if (auto *settings = root.child("settings"))
    stage.ui.root()["settings"] = *settings;

  vsr::scene::Scene stagedScene;
  vsr::animation::AnimationManager stagedAnimations(&stagedScene);
  if (!reconstructProject(
          *state, stage.project, stagedScene, stagedAnimations, false, error)) {
    return false;
  }

  stage.m_state = std::move(state);
  return true;
}

bool applyProjectOpen(ProjectOpenStage &stage,
    vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animationManager,
    std::string *error)
{
  if (!stage.m_state)
    return fail("project open has not been staged", error);
  return reconstructProject(
      *stage.m_state, stage.project, scene, animationManager, true, error);
}

} // namespace vsr::scivis_studio
