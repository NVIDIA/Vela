// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Project.h"
#include "ProjectPersistence.h"

#include "vsr/app/Context.h"
#include "vsr/io/importers.hpp"
#include "vsr/scene/objects/Array.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace vsr::scivis_studio {

struct DatasetDirtyDelegate;

struct FileAnimationDatasetOptions
{
  bool setActiveShotFrameCount{true};
};

struct DatasetCandidate
{
  std::filesystem::path file;
  std::string proposedName;
};

struct ProjectContext
{
  ProjectContext() = default;
  explicit ProjectContext(vsr::app::Context *ctx);
  ~ProjectContext();

  void setAppContext(vsr::app::Context *ctx);
  vsr::app::Context *appContext() const;

  Project &project();
  const Project &project() const;

  void createUnsavedProject();
  bool addShot(const std::string &name = "");
  // Shot edits as whole operations (the monolith's editors mutate Shot fields
  // inline; the remote server needs them as validated calls). removeShot
  // refuses the last shot and, when the active shot goes, makes the first
  // remaining one active. updateShot replaces the stored Shot with a
  // validated copy of `shot`: unknown rig ids and renderers of another
  // library are rejected, bindings to unknown datasets are dropped, frame
  // fields are clamped, the runtime camera ref is kept and `playing` is
  // ignored (playback is driven through the AnimationManager, not this
  // call). setActiveShot switches shots and re-syncs the animation manager.
  bool removeShot(const ShotID &id, std::string *error = nullptr);
  // Replaces the shot wholesale after validation, never honouring `playing`
  // and, while the shot plays, keeping the frame it is on.
  bool updateShot(const Shot &shot, std::string *error = nullptr);
  bool setActiveShot(const ShotID &id, std::string *error = nullptr);
  // Playback as whole operations (the monolith's transport pokes the
  // AnimationManager and the Shot inline). setPlaying accepts the active shot
  // only, starts or stops the manager and writes shot.playing.
  // setActiveShotFrame seeks the active shot to `frame` (clamped); the
  // manager's time-changed callback lands it in shot.currentFrame and applies
  // the shot. When a non-looping shot plays off its end, the manager's
  // stopped callback writes playing=false and currentFrame=last. None of
  // these dirty the project: the playback position is transient state, as
  // when the monolith's tick writes it.
  bool setPlaying(const ShotID &id, bool playing, std::string *error = nullptr);
  void setActiveShotFrame(int frame);
  Dataset *addStaticDataset(const std::string &name,
      const std::filesystem::path &sourcePath,
      vsr::io::ImporterType importerType);
  // Load a VSR Layer Subtree Archive (as saved from vsrViewer's LayerTree) as
  // a Static Dataset. The subtree becomes embedded dataset content; the next
  // save writes it into the project as a Dataset Archive.
  Dataset *addStaticDatasetFromSubtree(
      const std::string &name, const std::filesystem::path &sourcePath);
  Dataset *addFileAnimationDataset(const std::string &name,
      const std::vector<std::filesystem::path> &sourcePaths,
      vsr::io::ImporterType importerType,
      const FileAnimationDatasetOptions &options = {});
  // Create a Declared Dataset from its Source List alone: no source file is
  // read — not even an existence check — so declaring behaves identically on
  // every machine (ADR 0014). The dataset records Unloaded residency; the
  // first successful Dataset Load materializes it.
  Dataset *addDeclaredFileAnimationDataset(const std::string &name,
      const std::vector<std::string> &sourceList,
      vsr::io::ImporterType importerType,
      const FileAnimationDatasetOptions &options = {});
  bool renameDataset(const DatasetID &id,
      const std::string &newName,
      std::string *error = nullptr);
  bool removeDataset(const DatasetID &id,
      bool keepAssetFile = false,
      std::string *error = nullptr);
  bool reimportStaticDataset(const DatasetID &id, std::string *error = nullptr);
  // Dataset Load/Unload flip an inventory dataset's residency. Unload requires
  // a clean dataset and never touches disk; Load recreates the runtime from
  // the managed asset and changes nothing when it fails. Both are no-ops when
  // the dataset already has the requested residency.
  bool loadDataset(const DatasetID &id, std::string *error = nullptr);
  bool unloadDataset(const DatasetID &id, std::string *error = nullptr);
  // Cheap on-demand availability hint for an Unloaded dataset: a missing
  // asset file is definitively Unavailable. The check never upgrades status —
  // the authoritative assessment is the load attempt itself.
  void refreshUnloadedDatasetAvailability(Dataset &dataset) const;
  bool saveDatasetArchive(const DatasetID &id,
      const std::filesystem::path &file,
      std::string *error = nullptr);
  Dataset *loadDatasetArchive(
      const std::filesystem::path &file, std::string *error = nullptr);
  std::vector<DatasetCandidate> discoverDatasetCandidates() const;
  Dataset *incorporateDatasetCandidate(const DatasetCandidate &candidate,
      const std::string &name,
      std::string *error = nullptr);
  void applyActiveShot();
  void syncAnimationManagerToActiveShot();

  bool saveProject(const std::filesystem::path &directory,
      vsr::core::DataNode *windows = nullptr,
      const std::string &layout = "",
      vsr::core::DataNode *settings = nullptr,
      std::string *error = nullptr);
  bool openProject(const std::filesystem::path &directory,
      vsr::core::DataNode *windowsOut = nullptr,
      std::string *layoutOut = nullptr,
      vsr::core::DataNode *settingsOut = nullptr,
      std::string *error = nullptr,
      const ProjectOpenOptions &options = {});
  // The second half of openProject(): installs a stage that stageProjectOpen()
  // filled, replacing the live Scene and Project. Staging reads the directory
  // without touching shared state, so a caller may run it elsewhere and
  // finish here; on failure the live project is unchanged.
  bool openStagedProject(ProjectOpenStage &stage,
      vsr::core::DataNode *windowsOut = nullptr,
      std::string *layoutOut = nullptr,
      vsr::core::DataNode *settingsOut = nullptr,
      std::string *error = nullptr);

  vsr::scene::LayerNodeRef resolve(const SceneNodeRef &ref) const;
  vsr::scene::Object *resolve(const SceneObjectRef &ref) const;
  SceneNodeRef refFor(
      const std::string &layerName, vsr::scene::LayerNodeRef ref) const;
  vsr::scene::LayerNodeRef resolveDatasetRoot(Dataset &dataset);
  vsr::scene::LayerNodeRef resolveLightRigRoot(LightRig &rig);
  vsr::scene::Object *resolveShotCamera(Shot &shot);
  LightRig *createLightRig(const std::string &name = "");
  LightRig *cloneLightRig(const LightRigID &id);
  bool removeLightRig(const LightRigID &id);
  // In-memory rename; rejects (returns false + error) an invalid format or a
  // name already used case-insensitively by another rig in the same collection.
  bool renameLightRig(const LightRigID &id,
      const std::string &newName,
      std::string *error = nullptr);
  bool renameCameraRig(const CameraRigID &id,
      const std::string &newName,
      std::string *error = nullptr);
  vsr::scene::LayerNodeRef addLightToRig(
      LightRig &rig, const std::string &subtype);
  bool removeLightFromRig(LightRig &rig, vsr::scene::LayerNodeRef lightNode);
  int shotUseCount(const LightRigID &id) const;
  CameraRig *createCameraRig(const std::string &name = "");
  bool removeCameraRig(const CameraRigID &id);
  int cameraRigUseCount(const CameraRigID &id) const;
  CameraRig *activeShotCameraRig();

  // Color maps: a ColorMapRecord paired with a scene Array of RGBA samples
  // named "<colorMapId>_colormap" (the record carries no scene ref, so the
  // name is the link, as with "<shotId>_camera"). Create makes both, remove
  // removes both; rename touches the record only.
  ColorMapRecord *createColorMap(const std::string &name = "");
  bool renameColorMap(const ColorMapID &id,
      const std::string &newName,
      std::string *error = nullptr);
  bool removeColorMap(const ColorMapID &id, std::string *error = nullptr);
  vsr::scene::ArrayRef resolveColorMapArray(const ColorMapID &id) const;

  // Standalone rig Archive IO. Save writes the named rig to a .vsr file; Load
  // adds a new library entry (with a fresh id and a de-duplicated name) and
  // never alters shot bindings. Load returns the new rig, or nullptr on error.
  bool saveCameraRigArchive(const CameraRigID &id,
      const std::filesystem::path &file,
      std::string *error = nullptr);
  CameraRig *loadCameraRigArchive(
      const std::filesystem::path &file, std::string *error = nullptr);
  bool saveLightRigArchive(const LightRigID &id,
      const std::filesystem::path &file,
      std::string *error = nullptr);
  LightRig *loadLightRigArchive(
      const std::filesystem::path &file, std::string *error = nullptr);

 private:
  friend struct DatasetDirtyDelegate;
  // Shot semantics shared by eager and declared file-animation creates: bind
  // the new dataset to every shot, enabled only in the active one, and drive
  // the active shot's frame count from the source-list length.
  void applyFileAnimationShotSemantics(const Dataset &record,
      size_t frameCount,
      const FileAnimationDatasetOptions &options);
  void installDatasetDirtyDelegate();
  void markDatasetDirtyForObject(const vsr::scene::Object *object);
  Dataset *loadDatasetArchiveImpl(const std::filesystem::path &file,
      const std::string &name,
      bool alreadyManaged,
      std::string *error);
  vsr::scene::LayerNodeRef ensureChild(
      vsr::scene::LayerNodeRef parent, const char *name);
  vsr::scene::LayerNodeRef ensureStudioRoot();
  vsr::scene::LayerNodeRef ensureDatasetsRoot();
  vsr::scene::LayerNodeRef ensureShotsRoot();
  vsr::scene::LayerNodeRef ensureLightRigsRoot();
  void resetScene();
  void ensureRendererDefaults(Shot &shot);
  LightRig *ensureDefaultLightRig();
  CameraRig *ensureDefaultCameraRig();
  vsr::scene::ArrayRef createColorMapArray(const ColorMapID &id);
  // Records loaded from a manifest have no Array yet: give each a default.
  void ensureColorMapArrays();
  void installAnimationManagerCallback();
  void updateActiveShotFromAnimationTime();
  void onAnimationPlaybackStopped();
  // The manager's frame and playing flag into `shot`, clamped to its ranges.
  void writeAnimationStateToShot(Shot &shot) const;

  vsr::app::Context *m_ctx{nullptr};
  Project m_project;
  std::vector<std::filesystem::path> m_pendingAssetRemovals;
  bool m_syncingAnimationManager{false};
  // Residency operations rebuild or tear down whole dataset subtrees; the
  // per-object dirty tracking is meaningless (and O(n^2)) while they run.
  bool m_mutatingDatasetRuntime{false};
  vsr::scene::BaseUpdateDelegate *m_datasetDirtyDelegate{nullptr};
};

const char *toString(vsr::io::ImporterType importerType);
vsr::io::ImporterType importerTypeFromString(const std::string &s);

} // namespace vsr::scivis_studio
