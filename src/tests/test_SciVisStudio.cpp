// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "catch.hpp"

#include "CameraRig.h"
#include "ColorMaps.h"
#include "DatasetIO.h"
#include "LightRig.h"
#include "ProjectContext.h"
#include "ProjectPersistence.h"
#include "ProjectSerialization.h"
#include "RenderShot.h"
#include "RenderShotCLI.h"
#include "ShotOps.h"
#include "StudioCLI.h"

#include "vsr/app/ApplicationDump.h"
#include "vsr/app/Context.h"
#include "vsr/app/LegacyApplicationContext.h"
#include "vsr/core/DataPath.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/core/DataTreeMetadata.hpp"
#include "vsr/io/animation/SpatialFieldFileBinding.hpp"
#include "vsr/io/archives/CameraArchive.hpp"
#include "vsr/io/archives/RendererArchive.hpp"
#include "vsr/io/archives/SceneArchive.hpp"
#include "vsr/io/serialization/serialization_internal.hpp"
#include "vsr/scene/UpdateDelegate.hpp"
#include "vsr/scene/objects/Camera.hpp"
#include "vsr/scene/objects/Geometry.hpp"
#include "vsr/scene/objects/Light.hpp"
#include "vsr/scene/objects/Material.hpp"
#include "vsr/scene/objects/Renderer.hpp"
#include "vsr/scene/objects/SpatialField.hpp"
#include "vsr/scene/objects/Volume.hpp"
// anari
#include <anari/anari_cpp.hpp>
// std
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

using namespace vsr::scivis_studio;

namespace {

struct CountingLayerUpdateDelegate : public vsr::scene::EmptyUpdateDelegate
{
  void signalLayerStructureUpdated(const vsr::scene::Layer *l) override
  {
    lastLayer = l;
    layerStructureUpdates++;
  }

  const vsr::scene::Layer *lastLayer{nullptr};
  int layerStructureUpdates{0};
};

// Build the minimal file-animation dataset runtime — a volume with an initial
// spatial field plus one runtime file animation over the given paths — and
// return its dataset metadata.
Dataset makeFileAnimationDatasetRuntime(vsr::scene::Scene &scene,
    vsr::animation::AnimationManager &animations,
    vsr::scene::LayerNodeRef parent,
    const std::string &id,
    const std::string &name,
    const std::vector<std::string> &paths,
    vsr::scene::LayerNodeRef *rootOut = nullptr)
{
  auto field = scene.createObject<vsr::scene::SpatialField>(
      vsr::scene::tokens::spatial_field::structuredRegular);
  auto voxels = scene.createArray(ANARI_FLOAT32, 1, 1, 1);
  *voxels->mapAs<float>() = 1.f;
  voxels->unmap();
  field->setParameterObject("data", *voxels);
  auto volume = scene.createObject<vsr::scene::Volume>(
      vsr::scene::tokens::volume::transferFunction1D);
  volume->setParameterObject("value", *field);
  auto root = scene.insertChildNode(parent, id.c_str());
  scene.insertChildObjectNode(root, volume, "volume");
  animations.addAnimation(name + " file animation")
      .emplaceFileBinding<vsr::io::SpatialFieldFileBinding>(
          &scene, volume.data(), field, paths);
  if (rootOut)
    *rootOut = root;

  Dataset dataset;
  dataset.id = id;
  dataset.name = name;
  dataset.sourceKind = DatasetSourceKind::FileAnimation;
  dataset.importerType = "VOLUME_ANIMATION";
  for (const auto &path : paths)
    dataset.sourceFiles.push_back({path});
  return dataset;
}

std::string fileContents(const std::filesystem::path &file)
{
  std::ifstream in(file, std::ios::binary);
  std::stringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

} // namespace

SCENARIO("SciVis Studio persistence plans a complete new project save",
    "[SciVisStudio][ProjectPersistence]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_persistence_save_plan";
  std::filesystem::remove_all(root);

  Project project;
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);
  ProjectSaveResult result;
  std::string error;

  REQUIRE(buildProjectSavePlan(
      ProjectSaveRequest(project, scene, animations, root), result, &error));
  REQUIRE_FALSE(std::filesystem::exists(root));
  REQUIRE(result.project.name == root.filename().string());
  REQUIRE(result.project.projectDirectory == root);
  REQUIRE_FALSE(result.project.dirty);
  REQUIRE(result.plan.directory == root);
  REQUIRE(result.plan.directories
      == std::vector<std::filesystem::path>{
          "renders", "datasets", "cameras", "lights", "scene"});
  REQUIRE(result.plan.assets.size() == 2);
  REQUIRE(result.plan.assets[0].target
      == std::filesystem::path("scene/cameras.vsr"));
  REQUIRE(result.plan.assets[1].target
      == std::filesystem::path("scene/renderers.vsr"));
  REQUIRE(result.plan.manifest.target == PROJECT_MANIFEST_FILENAME);
}

SCENARIO("SciVis Studio persistence stages a project before applying it",
    "[SciVisStudio][ProjectPersistence]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_persistence_open_stage";
  std::filesystem::remove_all(root);

  {
    vsr::app::Context appContext;
    ProjectContext context(&appContext);
    context.createUnsavedProject();
    context.project().name = "Staged Project";
    REQUIRE(context.saveProject(root));
  }

  ProjectOpenStage stage;
  std::string error;
  REQUIRE(stageProjectOpen(root, stage, {}, &error));
  REQUIRE(stage.project.name == "Staged Project");
  REQUIRE(stage.project.projectDirectory == root);

  // Applying uses the decoded stage rather than reopening project files.
  std::filesystem::remove_all(root);

  vsr::scene::Scene target;
  vsr::animation::AnimationManager targetAnimations(&target);
  const auto cameraCountBefore = target.numberOfObjects(ANARI_CAMERA);
  REQUIRE(cameraCountBefore == 1);
  REQUIRE(applyProjectOpen(stage, target, targetAnimations, &error));
  REQUIRE(target.numberOfObjects(ANARI_CAMERA) >= cameraCountBefore);
  REQUIRE(target.layer("studio") != nullptr);
  REQUIRE(stage.project.cameraRigs.size() == 1);
  REQUIRE(stage.project.lightRigs.size() == 1);

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio static Dataset Archives are self-contained",
    "[SciVisStudio]")
{
  const auto file = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_static_dataset.vsr";
  std::filesystem::remove(file);

  vsr::scene::Scene source;
  vsr::animation::AnimationManager sourceAnimations(&source);
  auto geometry = source.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  geometry->setName("animated geometry");
  auto material = source.createObject<vsr::scene::Material>(
      vsr::scene::tokens::material::matte);
  auto surface = source.createSurface("surface", geometry, material);
  auto root =
      source.insertChildNode(source.defaultLayer()->root(), "dataset_0042");
  source.insertChildObjectNode(root, surface, "surface");

  const float times[] = {0.f, 1.f};
  const float radii[] = {0.5f, 1.5f};
  sourceAnimations.addAnimation("radius").addObjectParameterBinding(
      geometry.data(), "radius", ANARI_FLOAT32, radii, times, 2);

  Dataset dataset;
  dataset.id = "dataset_0042";
  dataset.name = "Example Dataset";
  dataset.sourceKind = DatasetSourceKind::Static;
  dataset.importerType = "OBJ";
  dataset.source.sourcePath = "../source/example.obj";
  dataset.source.importerSettings.set("flatten", "false");
  REQUIRE(saveDatasetArchiveFile(dataset, root, sourceAnimations, file));

  auto validation = validateDatasetAsset(file);
  REQUIRE(validation.ok);
  REQUIRE(validation.dataset.id.empty());
  REQUIRE(validation.dataset.name == "Example Dataset");
  REQUIRE(validation.dataset.source.sourcePath == "../source/example.obj");
  REQUIRE(validation.dataset.source.importerSettings.size() == 1);
  REQUIRE(validation.dataset.source.importerSettings.at_index(0).first
      == "flatten");

  vsr::core::DataTree serialized;
  REQUIRE(serialized.load(file.string().c_str()));
  REQUIRE(serialized.root()["dataset"].child("id") == nullptr);
  REQUIRE(serialized.root()["subtree"]["name"].getValueAs<std::string>()
      == "Example Dataset");

  vsr::scene::Scene target;
  vsr::animation::AnimationManager targetAnimations(&target);
  auto destination =
      target.insertChildNode(target.defaultLayer()->root(), "datasets");
  Dataset loadedDataset;
  vsr::scene::LayerNodeRef loadedRoot;
  REQUIRE(loadDatasetArchiveFile(
      target, targetAnimations, file, destination, loadedDataset, loadedRoot));
  REQUIRE(loadedRoot);
  REQUIRE(loadedDataset.status == DatasetStatus::Available);
  REQUIRE(targetAnimations.animations().size() == 1);
  REQUIRE(targetAnimations.animations()
              .front()
              .objectParameterBindings()
              .front()
              .target()
              ->name()
      == "animated geometry");

  Dataset secondImport;
  vsr::scene::LayerNodeRef secondRoot;
  REQUIRE(loadDatasetArchiveFile(
      target, targetAnimations, file, destination, secondImport, secondRoot));
  REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 2);
  REQUIRE(target.getObject(ANARI_GEOMETRY, 0)
      != target.getObject(ANARI_GEOMETRY, 1));
  // One default material plus one independently loaded material per dataset.
  REQUIRE(target.numberOfObjects(ANARI_MATERIAL) == 3);

  std::filesystem::remove(file);
}

SCENARIO(
    "SciVis Studio file-animation Dataset Archives externalize their "
    "source list",
    "[SciVisStudio]")
{
  const auto file = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_file_animation_dataset.vsr";
  const auto corruptFile = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_file_animation_dataset_corrupt.vsr";
  std::filesystem::remove(file);
  std::filesystem::remove(sourceListFilePath(file));
  std::filesystem::remove(corruptFile);

  vsr::scene::Scene source;
  vsr::animation::AnimationManager sourceAnimations(&source);
  const std::vector<std::string> paths = {
      "relative/frame 01.raw", "/missing/../opaque/frame02.raw"};
  vsr::scene::LayerNodeRef root;
  auto dataset = makeFileAnimationDatasetRuntime(source,
      sourceAnimations,
      source.defaultLayer()->root(),
      "dataset_0001",
      "Opaque Frames",
      paths,
      &root);
  Dataset invalidStatic = dataset;
  invalidStatic.sourceKind = DatasetSourceKind::Static;
  invalidStatic.importerType = "VOLUME";
  std::string invalidStaticError;
  REQUIRE_FALSE(saveDatasetArchiveFile(
      invalidStatic, root, sourceAnimations, corruptFile, &invalidStaticError));
  REQUIRE(invalidStaticError.find("cannot own file animations")
      != std::string::npos);
  std::string saveError;
  const bool saved =
      saveDatasetArchiveFile(dataset, root, sourceAnimations, file, &saveError);
  INFO(saveError);
  REQUIRE(saved);

  vsr::core::DataTree corrupt;
  REQUIRE(corrupt.load(file.string().c_str()));
  auto &derived = corrupt.root()["animations"].append();
  derived["name"] = "competing runtime authority";
  auto &fileBinding = derived["fileBindings"].append();
  fileBinding["kind"] = "spatialField";
  fileBinding["targetIndex"] = size_t(0);
  REQUIRE(corrupt.save(corruptFile.string().c_str()));
  auto corruptValidation = validateDatasetAsset(corruptFile);
  REQUIRE_FALSE(corruptValidation.ok);
  REQUIRE(corruptValidation.error.find("derived runtime file bindings")
      != std::string::npos);

  // New-format dataset files persist importer settings but no frame paths.
  vsr::core::DataTree serialized;
  REQUIRE(serialized.load(file.string().c_str()));
  REQUIRE(serialized.root()["dataset"].child("sourceFiles") == nullptr);
  REQUIRE(datasetArchiveUsesSourceListFile(serialized.root()));

  // Dataset Archive Load fails cleanly without the sibling Source List File.
  {
    vsr::scene::Scene target;
    vsr::animation::AnimationManager targetAnimations(&target);
    Dataset missingDataset;
    vsr::scene::LayerNodeRef missingRoot;
    std::string missingError;
    REQUIRE_FALSE(loadDatasetArchiveFile(target,
        targetAnimations,
        file,
        target.defaultLayer()->root(),
        missingDataset,
        missingRoot,
        &missingError));
    REQUIRE(missingError.find("Source List File") != std::string::npos);
    REQUIRE(target.numberOfObjects(ANARI_VOLUME) == 0);
    REQUIRE_FALSE(validateDatasetAsset(file).ok);
  }

  REQUIRE(writeSourceListFile(sourceListFilePath(file), dataset.sourceFiles));
  auto pairValidation = validateDatasetAsset(file);
  REQUIRE(pairValidation.ok);
  REQUIRE(pairValidation.dataset.sourceFiles.size() == paths.size());

  vsr::scene::Scene target;
  vsr::animation::AnimationManager targetAnimations(&target);
  Dataset loadedDataset;
  vsr::scene::LayerNodeRef importedRoot;
  REQUIRE(loadDatasetArchiveFile(target,
      targetAnimations,
      file,
      target.defaultLayer()->root(),
      loadedDataset,
      importedRoot));
  REQUIRE_FALSE(loadedDataset.pendingSourceListMigration);
  REQUIRE(loadedDataset.sourceFiles.size() == paths.size());
  REQUIRE(loadedDataset.sourceFiles[0].path == paths[0]);
  REQUIRE(loadedDataset.sourceFiles[1].path == paths[1]);
  // The relative entry is anchored once, at read, to the Source List File's
  // directory; the absolute entry stays opaque.
  const auto anchored =
      (std::filesystem::temp_directory_path() / paths[0]).string();
  REQUIRE(loadedDataset.sourceFiles[0].resolvedPath == anchored);
  REQUIRE(loadedDataset.sourceFiles[1].resolvedPath.empty());
  REQUIRE(targetAnimations.animations().size() == 1);
  REQUIRE(targetAnimations.animations().front().fileBindings().size() == 1);

  vsr::core::DataTree binding;
  targetAnimations.animations().front().fileBindings().front()->toDataNode(
      binding.root());
  REQUIRE(
      binding.root()["files"].child(0)->getValueAs<std::string>() == anchored);
  REQUIRE(
      binding.root()["files"].child(1)->getValueAs<std::string>() == paths[1]);

  std::filesystem::remove(file);
  std::filesystem::remove(sourceListFilePath(file));
  std::filesystem::remove(corruptFile);
}

SCENARIO("SciVis Studio Source List Files hold one trimmed path per line",
    "[SciVisStudio]")
{
  const auto directory = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_source_list_files";
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  const auto file = directory / "Frames.sources";

  GIVEN("A hand-written Source List File")
  {
    {
      std::ofstream out(file);
      out << "\n";
      out << "  relative/frame 01.raw \t\n";
      out << "\t\n";
      out << "/absolute/frame02.raw\n";
      out << "trailing/frame03.raw"; // no final newline
    }

    THEN("Entries read back trimmed, in line order, skipping blanks")
    {
      std::vector<DatasetSourceFile> entries;
      REQUIRE(readSourceListFile(file, entries));
      REQUIRE(entries.size() == 3);
      REQUIRE(entries[0].path == "relative/frame 01.raw");
      REQUIRE(entries[1].path == "/absolute/frame02.raw");
      REQUIRE(entries[2].path == "trailing/frame03.raw");
      REQUIRE(entries[0].resolvedPath
          == (directory / "relative/frame 01.raw").string());
      REQUIRE(entries[1].resolvedPath.empty());

      AND_THEN("Writing the entries back reproduces the raw lines verbatim")
      {
        const auto rewritten = directory / "Rewritten.sources";
        REQUIRE(writeSourceListFile(rewritten, entries));
        std::ifstream in(rewritten);
        std::stringstream contents;
        contents << in.rdbuf();
        REQUIRE(contents.str()
            == "relative/frame 01.raw\n/absolute/frame02.raw\n"
               "trailing/frame03.raw\n");
      }
    }
  }

  GIVEN("A missing, empty, or blank Source List File")
  {
    THEN("Reading fails with a clear error")
    {
      std::vector<DatasetSourceFile> entries;
      std::string error;
      REQUIRE_FALSE(
          readSourceListFile(directory / "Missing.sources", entries, &error));
      REQUIRE_FALSE(error.empty());

      const auto blank = directory / "Blank.sources";
      {
        std::ofstream out(blank);
        out << " \n\t\n";
      }
      error.clear();
      REQUIRE_FALSE(readSourceListFile(blank, entries, &error));
      REQUIRE(error.find("empty") != std::string::npos);
    }
  }

  GIVEN("An empty File Animation Source List")
  {
    THEN("Writing refuses to produce an unloadable dataset")
    {
      std::string error;
      REQUIRE_FALSE(writeSourceListFile(file, {}, &error));
    }
  }

  std::filesystem::remove_all(directory);
}

SCENARIO("SciVis Studio legacy embedded sourceFiles load and mark migration",
    "[SciVisStudio]")
{
  const auto file = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_legacy_embedded_dataset.vsr";
  std::filesystem::remove(file);
  std::filesystem::remove(sourceListFilePath(file));

  vsr::scene::Scene source;
  vsr::animation::AnimationManager sourceAnimations(&source);
  const std::vector<std::string> paths = {
      "legacy/frame01.raw", "/legacy/frame02.raw"};
  vsr::scene::LayerNodeRef root;
  auto dataset = makeFileAnimationDatasetRuntime(source,
      sourceAnimations,
      source.defaultLayer()->root(),
      "dataset_0001",
      "Legacy Frames",
      paths,
      &root);
  REQUIRE(saveDatasetArchiveFile(dataset, root, sourceAnimations, file));

  // Recreate the legacy on-disk format: the dataset file embeds the list.
  {
    vsr::core::DataTree legacy;
    REQUIRE(legacy.load(file.string().c_str()));
    auto &files = legacy.root()["dataset"]["sourceFiles"];
    for (const auto &path : paths)
      files.append() = path;
    REQUIRE_FALSE(datasetArchiveUsesSourceListFile(legacy.root()));
    REQUIRE(legacy.save(file.string().c_str()));
  }

  auto validation = validateDatasetAsset(file);
  REQUIRE(validation.ok);
  REQUIRE(validation.dataset.pendingSourceListMigration);
  REQUIRE(validation.dataset.sourceFiles.size() == paths.size());

  vsr::scene::Scene target;
  vsr::animation::AnimationManager targetAnimations(&target);
  Dataset loadedDataset;
  vsr::scene::LayerNodeRef importedRoot;
  REQUIRE(loadDatasetArchiveFile(target,
      targetAnimations,
      file,
      target.defaultLayer()->root(),
      loadedDataset,
      importedRoot));
  REQUIRE(loadedDataset.pendingSourceListMigration);
  REQUIRE(loadedDataset.sourceFiles.size() == paths.size());
  REQUIRE(loadedDataset.sourceFiles[0].path == paths[0]);
  // Legacy entries stay opaque: no read-time anchoring is applied.
  REQUIRE(loadedDataset.sourceFiles[0].resolvedPath.empty());
  REQUIRE(targetAnimations.animations().size() == 1);

  vsr::core::DataTree binding;
  targetAnimations.animations().front().fileBindings().front()->toDataNode(
      binding.root());
  REQUIRE(
      binding.root()["files"].child(0)->getValueAs<std::string>() == paths[0]);

  std::filesystem::remove(file);
}

SCENARIO("SciVis Studio project model serialization", "[SciVisStudio]")
{
  GIVEN("A project with datasets, shots, light rigs, and camera rigs")
  {
    Project project;
    project.name = "RoundTrip";
    project.projectDirectory = "/tmp/roundtrip";
    Dataset dataset;
    dataset.id = "dataset_0001";
    dataset.name = "Dataset";
    dataset.sourceKind = DatasetSourceKind::Static;
    dataset.importerType = "OBJ";
    dataset.source.sourcePath = "/tmp/data.obj";
    dataset.status = DatasetStatus::Available;
    dataset.rootNode = {"studio", 3};
    project.datasets.push_back(std::move(dataset));

    Shot shot;
    shot.id = "shot_0001";
    shot.name = "Shot 1";
    shot.datasetBindings.push_back({"dataset_0001", true});
    shot.lightRigId = "lightRig_0001";
    shot.cameraRigId = "cameraRig_0001";
    shot.camera = {ANARI_CAMERA, 2};
    shot.renderSettings.rendererLibrary = "dummy_test_device";
    shot.renderSettings.rendererObjectIndex = 7;
    shot.renderSettings.rendererSubtype = "dummy_test_renderer";

    CameraRig cameraRig;
    cameraRig.id = "cameraRig_0001";
    cameraRig.name = "Default Camera";
    CameraKeyframe keyframe;
    keyframe.frame = 12;
    keyframe.name = "mid";
    keyframe.manipulator.orbit.lookat = {1.f, 2.f, 3.f};
    keyframe.manipulator.orbit.azeldist = {10.f, 20.f, 30.f};
    keyframe.interpolationToNext = CameraInterpolation::EaseOutIn;
    cameraRig.keyframes.push_back(keyframe);
    project.activeShotId = shot.id;
    project.shots.push_back(shot);
    project.lightRigs.push_back({"lightRig_0001", "Default", {"studio", 5}});
    project.cameraRigs.push_back(std::move(cameraRig));

    vsr::core::DataTree tree;
    projectToNode(project, tree.root()["scivisStudio"], ProjectForm::Manifest);
    auto &serialized = tree.root()["scivisStudio"];

    REQUIRE(serialized["datasets"].child(0)->child("rootNode") == nullptr);
    REQUIRE(serialized["datasets"].child(0)->child("sourceKind") == nullptr);
    REQUIRE(serialized["datasets"].child(0)->child("sourceFiles") == nullptr);
    REQUIRE(serialized["datasets"].child(0)->child("source") == nullptr);
    REQUIRE(serialized["shots"].child(0)->child("lightRigId") != nullptr);
    REQUIRE(serialized["shots"].child(0)->child("cameraRigId") != nullptr);
    REQUIRE(serialized["shots"].child(0)->child("cameraRig") == nullptr);
    REQUIRE(serialized["shots"].child(0)->child("camera") == nullptr);
    REQUIRE(serialized["lightRigs"].child(0)->child("rootNode") == nullptr);
    // v4: camera-rig keyframe data lives in cameras/<name>.vsr, not the
    // manifest.
    REQUIRE(serialized["cameraRigs"].child(0)->child("rig") == nullptr);
    REQUIRE(serialized["cameraRigs"].child(0)->child("name") != nullptr);

    Project loaded;
    REQUIRE(nodeToProject(serialized, loaded, ProjectForm::Manifest));

    THEN("IDs and bindings survive the manifest round trip")
    {
      REQUIRE(loaded.datasets.size() == 1);
      REQUIRE(loaded.datasets.front().id == "dataset_0001");
      REQUIRE(loaded.datasets.front().name == "Dataset");
      REQUIRE(loaded.datasets.front().sourceFiles.empty());
      REQUIRE(loaded.datasets.front().status == DatasetStatus::Unavailable);
      REQUIRE(loaded.shots.size() == 1);
      REQUIRE(loaded.shots.front().id == "shot_0001");
      REQUIRE(loaded.shots.front().lightRigId == "lightRig_0001");
      REQUIRE(loaded.shots.front().cameraRigId == "cameraRig_0001");
      REQUIRE(loaded.lightRigs.size() == 1);
      REQUIRE(loaded.lightRigs.front().id == "lightRig_0001");
      REQUIRE(loaded.cameraRigs.size() == 1);
      REQUIRE(loaded.cameraRigs.front().id == "cameraRig_0001");
      REQUIRE(loaded.cameraRigs.front().name == "Default Camera");
      REQUIRE(loaded.shots.front().renderSettings.rendererLibrary
          == "dummy_test_device");
      REQUIRE(loaded.shots.front().renderSettings.rendererObjectIndex == 7);
      REQUIRE(loaded.shots.front().renderSettings.rendererSubtype
          == "dummy_test_renderer");
    }
  }
}

namespace {

// One line per node, indented by depth: name, then the value's ANARI type and
// text when it has one. append()ed children carry a '<n>' name minted by a
// process-wide counter, so they are spelled by ordinal instead; everything
// else in the manifest is deterministic.
void appendCanonicalDump(const vsr::core::DataNode &node,
    size_t ordinal,
    int depth,
    std::string &out)
{
  out.append(size_t(depth) * 2, ' ');
  if (vsr::core::isDelimitedNumber(node.name(),
          vsr::core::ANONYMOUS_NAME_OPEN,
          vsr::core::ANONYMOUS_NAME_CLOSE))
    out += "[" + std::to_string(ordinal) + "]";
  else
    out += node.name();

  const auto &v = node.getValue();
  if (v.valid()) {
    out += ": ";
    out += anari::toString(v.type());
    out += " ";
    if (v.is<std::string>())
      out += "\"" + v.getString() + "\"";
    else if (v.is<bool>())
      out += v.get<bool>() ? "true" : "false";
    else if (v.is<int>())
      out += std::to_string(v.get<int>());
    else if (v.is<uint32_t>())
      out += std::to_string(v.get<uint32_t>());
    else if (v.is<uint64_t>())
      out += std::to_string(v.get<uint64_t>());
    else if (v.is<float>()) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%g", double(v.get<float>()));
      out += buf;
    } else
      out += "?";
  }
  out += "\n";
  size_t childOrdinal = 0;
  node.foreach_child_const([&](const vsr::core::DataNode &child) {
    appendCanonicalDump(child, childOrdinal++, depth + 1, out);
  });
}

std::string canonicalDump(const vsr::core::DataNode &node)
{
  std::string out;
  appendCanonicalDump(node, 0, 0, out);
  return out;
}

// Every manifest field populated away from its default, plus runtime-only
// state the manifest must drop.
Project makeManifestGoldenProject()
{
  Project project;
  project.name = "Golden";
  project.projectDirectory = "/data/projects/golden";
  project.nextDatasetOrdinal = 7;
  project.dirty = true;

  Dataset wind;
  wind.id = "dataset_0001";
  wind.name = "Wind";
  wind.sourceKind = DatasetSourceKind::FileAnimation;
  wind.importerType = "VTK";
  wind.source.sourcePath = "/data/wind/frames.sources";
  wind.source.importerSettings.set("scalar", "velocity");
  wind.status = DatasetStatus::Available;
  wind.rootNode = {"datasets", 4};
  wind.sourceFiles.push_back({"frame_000.vtk", "/data/wind/frame_000.vtk"});
  wind.persistedName = "Wind";
  project.datasets.push_back(std::move(wind));

  Dataset parked;
  parked.id = "dataset_0005";
  parked.name = "Parked";
  parked.residency = DatasetResidency::Unloaded;
  project.datasets.push_back(std::move(parked));

  Shot intro;
  intro.id = "shot_0001";
  intro.name = "Intro";
  intro.frameCount = 240;
  intro.fps = 30.f;
  intro.currentFrame = 17;
  intro.playing = true;
  intro.loop = false;
  intro.datasetBindings.push_back({"dataset_0001", true});
  intro.datasetBindings.push_back({"dataset_0005", false});
  intro.lightRigId = "lightRig_0001";
  intro.cameraRigId = "cameraRig_0001";
  intro.camera = {ANARI_CAMERA, 2};
  intro.renderSettings.width = 1920;
  intro.renderSettings.height = 1080;
  intro.renderSettings.samples = 64;
  intro.renderSettings.rendererLibrary = "visrtx";
  intro.renderSettings.rendererObjectIndex = 9;
  intro.renderSettings.rendererSubtype = "scivis";
  intro.renderSettings.outputFilePrefix = "intro_";
  project.shots.push_back(intro);

  Shot outro;
  outro.id = "shot_0002";
  outro.name = "Outro";
  project.shots.push_back(outro);
  project.activeShotId = intro.id;

  LightRig lights;
  lights.id = "lightRig_0001";
  lights.name = "Studio Lights";
  lights.rootNode = {"lights", 11};
  lights.persistedName = "Studio Lights";
  project.lightRigs.push_back(lights);

  CameraRig rig;
  rig.id = "cameraRig_0001";
  rig.name = "Fly-through";
  rig.current.orbit.lookat = {1.f, 2.f, 3.f};
  CameraKeyframe keyframe;
  keyframe.frame = 12;
  keyframe.name = "mid";
  keyframe.interpolationToNext = CameraInterpolation::EaseOutIn;
  rig.keyframes.push_back(keyframe);
  rig.persistedName = "Fly-through";
  project.cameraRigs.push_back(rig);

  project.colorMaps.push_back({"colorMap_0001", "Viridis"});
  return project;
}

// What projectToNode() wrote for makeManifestGoldenProject() before the Shot
// and Project serializers were unified; the manifest form must not drift
// from it, since this is what project.vsr stores under "scivisStudio".
constexpr const char *MANIFEST_GOLDEN = R"(scivisStudio
  name: ANARI_STRING "Golden"
  projectDirectory: ANARI_STRING "/data/projects/golden"
  activeShot: ANARI_STRING "shot_0001"
  nextDatasetOrdinal: ANARI_UINT64 7
  dirty: ANARI_BOOL true
  datasets
    [0]
      id: ANARI_STRING "dataset_0001"
      name: ANARI_STRING "Wind"
      residency: ANARI_STRING "Loaded"
    [1]
      id: ANARI_STRING "dataset_0005"
      name: ANARI_STRING "Parked"
      residency: ANARI_STRING "Unloaded"
  shots
    [0]
      id: ANARI_STRING "shot_0001"
      name: ANARI_STRING "Intro"
      frameCount: ANARI_INT32 240
      fps: ANARI_FLOAT32 30
      currentFrame: ANARI_INT32 17
      playing: ANARI_BOOL true
      loop: ANARI_BOOL false
      lightRigId: ANARI_STRING "lightRig_0001"
      cameraRigId: ANARI_STRING "cameraRig_0001"
      renderSettings
        width: ANARI_UINT32 1920
        height: ANARI_UINT32 1080
        samples: ANARI_UINT32 64
        rendererLibrary: ANARI_STRING "visrtx"
        rendererObjectIndex: ANARI_UINT64 9
        rendererSubtype: ANARI_STRING "scivis"
        outputFilePrefix: ANARI_STRING "intro_"
      datasetBindings
        [0]
          datasetId: ANARI_STRING "dataset_0001"
          enabled: ANARI_BOOL true
        [1]
          datasetId: ANARI_STRING "dataset_0005"
          enabled: ANARI_BOOL false
    [1]
      id: ANARI_STRING "shot_0002"
      name: ANARI_STRING "Outro"
      frameCount: ANARI_INT32 120
      fps: ANARI_FLOAT32 24
      currentFrame: ANARI_INT32 0
      playing: ANARI_BOOL false
      loop: ANARI_BOOL true
      lightRigId: ANARI_STRING ""
      cameraRigId: ANARI_STRING ""
      renderSettings
        width: ANARI_UINT32 1024
        height: ANARI_UINT32 768
        samples: ANARI_UINT32 128
        rendererLibrary: ANARI_STRING ""
        rendererObjectIndex: ANARI_UINT64 18446744073709551615
        rendererSubtype: ANARI_STRING "default"
        outputFilePrefix: ANARI_STRING ""
      datasetBindings
  lightRigs
    [0]
      id: ANARI_STRING "lightRig_0001"
      name: ANARI_STRING "Studio Lights"
  cameraRigs
    [0]
      id: ANARI_STRING "cameraRig_0001"
      name: ANARI_STRING "Fly-through"
  colorMaps
    [0]
      id: ANARI_STRING "colorMap_0001"
      name: ANARI_STRING "Viridis"
)";

} // namespace

SCENARIO(
    "SciVis Studio manifest form matches its golden dump", "[SciVisStudio]")
{
  GIVEN("A project populated away from every default")
  {
    vsr::core::DataTree tree;
    projectToNode(makeManifestGoldenProject(),
        tree.root()["scivisStudio"],
        ProjectForm::Manifest);

    THEN("The manifest form is unchanged")
    {
      const auto dump = canonicalDump(tree.root()["scivisStudio"]);
      REQUIRE(dump == MANIFEST_GOLDEN);
    }

    THEN("Saving and reloading preserves the dump")
    {
      const auto file = std::filesystem::temp_directory_path()
          / "vsr_scivis_studio_golden_manifest.vsr";
      std::filesystem::remove(file);
      REQUIRE(tree.save(file.string().c_str()));
      vsr::core::DataTree loaded;
      REQUIRE(loaded.load(file.string().c_str()));
      std::filesystem::remove(file);
      REQUIRE(canonicalDump(loaded.root()["scivisStudio"]) == MANIFEST_GOLDEN);
    }

    THEN("Reading it back leaves the runtime fields at the open defaults")
    {
      Project loaded;
      REQUIRE(nodeToProject(
          tree.root()["scivisStudio"], loaded, ProjectForm::Manifest));
      REQUIRE(loaded.datasets.size() == 2);
      REQUIRE(loaded.datasets[0].status == DatasetStatus::Unavailable);
      REQUIRE_FALSE(loaded.datasets[0].dirty);
      REQUIRE(loaded.datasets[0].persistedName == "Wind");
      REQUIRE(loaded.datasets[0].rootNode.layerName.empty());
      REQUIRE(loaded.shots[0].camera.type == ANARI_UNKNOWN);
      REQUIRE(loaded.shots[0].datasetBindings.size() == 2);
      REQUIRE_FALSE(loaded.shots[0].datasetBindings[1].enabled);
      REQUIRE(loaded.cameraRigs[0].keyframes.empty());
    }
  }
}

SCENARIO("SciVis Studio rejects a malformed manifest", "[SciVisStudio]")
{
  GIVEN("A manifest written from a project")
  {
    vsr::core::DataTree tree;
    projectToNode(
        makeManifestGoldenProject(), tree.root(), ProjectForm::Manifest);
    Project loaded;

    THEN("A mistyped shot field is rejected")
    {
      (*tree.root()["shots"].child(0))["fps"] = std::string("fast");
      REQUIRE_FALSE(nodeToProject(tree.root(), loaded, ProjectForm::Manifest));
    }

    THEN("A dataset without an id is rejected")
    {
      auto &dataset = tree.root()["datasets"].append();
      dataset["name"] = std::string("nameless");
      REQUIRE_FALSE(nodeToProject(tree.root(), loaded, ProjectForm::Manifest));
    }

    THEN("An unknown residency spelling is rejected")
    {
      (*tree.root()["datasets"].child(1))["residency"] = std::string("Parked");
      REQUIRE_FALSE(nodeToProject(tree.root(), loaded, ProjectForm::Manifest));
    }

    THEN("A rejected read leaves the output untouched")
    {
      loaded.name = "Before";
      (*tree.root()["shots"].child(0))["fps"] = std::string("fast");
      REQUIRE_FALSE(nodeToProject(tree.root(), loaded, ProjectForm::Manifest));
      REQUIRE(loaded.name == "Before");
    }
  }
}

SCENARIO("SciVis Studio dataset residency round-trips through the manifest",
    "[SciVisStudio]")
{
  GIVEN("A project with a Loaded and an Unloaded dataset")
  {
    Project project;
    Dataset resident;
    resident.id = "dataset_0001";
    resident.name = "Resident";
    project.datasets.push_back(std::move(resident));
    Dataset parked;
    parked.id = "dataset_0002";
    parked.name = "Parked";
    parked.residency = DatasetResidency::Unloaded;
    project.datasets.push_back(std::move(parked));

    vsr::core::DataTree tree;
    projectToNode(project, tree.root(), ProjectForm::Manifest);

    THEN("Residency survives the manifest round trip")
    {
      Project loaded;
      REQUIRE(nodeToProject(tree.root(), loaded, ProjectForm::Manifest));
      REQUIRE(loaded.datasets.size() == 2);
      REQUIRE(loaded.datasets[0].residency == DatasetResidency::Loaded);
      REQUIRE(loaded.datasets[1].residency == DatasetResidency::Unloaded);
    }
  }

  GIVEN("A manifest whose datasets predate residency")
  {
    vsr::core::DataTree legacy;
    auto &d = legacy.root()["datasets"].append();
    d["id"] = std::string("dataset_0001");
    d["name"] = std::string("Old");

    THEN("An absent residency field means Loaded")
    {
      Project loaded;
      REQUIRE(nodeToProject(legacy.root(), loaded, ProjectForm::Manifest));
      REQUIRE(loaded.datasets.size() == 1);
      REQUIRE(loaded.datasets.front().residency == DatasetResidency::Loaded);
    }
  }
}

SCENARIO(
    "SciVis Studio dataset IDs are not reused after removal", "[SciVisStudio]")
{
  Project project;
  Dataset first;
  first.id = project::nextDatasetId(project);
  project.datasets.push_back(first);
  Dataset second;
  second.id = project::nextDatasetId(project);
  const auto removedId = second.id;
  project.datasets.push_back(second);
  project.datasets.pop_back();

  const auto next = project::nextDatasetId(project);
  REQUIRE(next == "dataset_0003");
  REQUIRE(next != removedId);

  vsr::core::DataTree tree;
  projectToNode(project, tree.root(), ProjectForm::Manifest);
  Project loaded;
  REQUIRE(nodeToProject(tree.root(), loaded, ProjectForm::Manifest));
  REQUIRE(project::nextDatasetId(loaded) == "dataset_0004");
}

SCENARIO("SciVis Studio Camera Rig Archives round-trip keyframe data",
    "[SciVisStudio]")
{
  CameraRig rig;
  rig.name = "Hero Cam";
  CameraKeyframe keyframe;
  keyframe.frame = 12;
  keyframe.name = "mid";
  keyframe.manipulator.orbit.lookat = {1.f, 2.f, 3.f};
  keyframe.interpolationToNext = CameraInterpolation::EaseOutIn;
  rig.keyframes.push_back(keyframe);

  const auto file =
      std::filesystem::temp_directory_path() / "vsr_camera_rig_roundtrip.vsr";
  std::filesystem::remove(file);

  REQUIRE(camera_rig::saveCameraRigArchiveFile(rig, file));

  CameraRig loaded;
  REQUIRE(camera_rig::loadCameraRigArchiveFile(file, loaded));

  REQUIRE(loaded.name == "Hero Cam");
  REQUIRE(loaded.keyframes.size() == 1);
  REQUIRE(loaded.keyframes.front().frame == 12);
  REQUIRE(loaded.keyframes.front().interpolationToNext
      == CameraInterpolation::EaseOutIn);

  std::filesystem::remove(file);
}

SCENARIO("SciVis Studio camera interpolation modes", "[SciVisStudio]")
{
  GIVEN("Camera interpolation modes")
  {
    THEN("String conversion round-trips all persisted values")
    {
      const CameraInterpolation modes[] = {CameraInterpolation::Hold,
          CameraInterpolation::Linear,
          CameraInterpolation::EaseOut,
          CameraInterpolation::EaseIn,
          CameraInterpolation::EaseOutIn};

      for (auto mode : modes)
        REQUIRE(camera_rig::interpolationFromString(camera_rig::toString(mode))
            == mode);

      REQUIRE_FALSE(camera_rig::interpolationFromString("Unknown").has_value());
    }

    THEN("Sampling applies easing to the segment interpolation factor")
    {
      CameraRig rig;

      CameraKeyframe a;
      a.frame = 0;
      a.manipulator.orbit.lookat = {0.f, 0.f, 0.f};
      a.manipulator.orbit.azeldist = {0.f, 0.f, 0.f};
      a.manipulator.orbit.fixedDist = 0.f;

      CameraKeyframe b;
      b.frame = 100;
      b.manipulator.orbit.lookat = {100.f, 0.f, 0.f};
      b.manipulator.orbit.azeldist = {100.f, 0.f, 0.f};
      b.manipulator.orbit.fixedDist = 100.f;

      rig.keyframes = {a, b};

      rig.keyframes.front().interpolationToNext = CameraInterpolation::EaseOut;
      REQUIRE(
          camera_rig::sampleCameraRig(rig, 25).orbit.lookat.x == Approx(6.25f));

      rig.keyframes.front().interpolationToNext = CameraInterpolation::EaseIn;
      REQUIRE(camera_rig::sampleCameraRig(rig, 25).orbit.lookat.x
          == Approx(57.8125f));

      rig.keyframes.front().interpolationToNext =
          CameraInterpolation::EaseOutIn;
      REQUIRE(camera_rig::sampleCameraRig(rig, 25).orbit.lookat.x
          == Approx(10.3515625f));
      REQUIRE(camera_rig::sampleCameraRig(rig, 25).orbit.azeldist.x
          == Approx(10.3515625f));
      REQUIRE(camera_rig::sampleCameraRig(rig, 25).orbit.fixedDist
          == Approx(10.3515625f));
      REQUIRE(camera_rig::sampleCameraRig(rig, 75).orbit.lookat.x
          == Approx(89.6484375f));
    }
  }
}

SCENARIO("SciVis Studio project root validation", "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_test_project";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  GIVEN("A valid metadata-tagged project manifest")
  {
    vsr::core::DataTree tree;
    vsr::core::writeDataTreeMetadata(tree.root(),
        {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
            PROJECT_FILE_TYPE,
            PROJECT_SCHEMA,
            SCHEMA_VERSION});
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
    std::filesystem::create_directories(root / "scene");
    vsr::scene::Scene scene;
    REQUIRE(vsr::io::save_CameraArchive(
        scene, (root / "scene/cameras.vsr").string().c_str()));
    REQUIRE(vsr::io::save_RendererArchive(
        scene, (root / "scene/renderers.vsr").string().c_str()));

    THEN("Validation succeeds")
    {
      auto result = validateProjectRoot(root);
      REQUIRE(result.ok);
    }
  }

  GIVEN("A valid legacy project manifest")
  {
    vsr::core::DataTree tree;
    tree.root()["projectKind"] = PROJECT_KIND;
    tree.root()["schemaVersion"] = 1;
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));

    THEN("Validation succeeds")
    {
      auto result = validateProjectRoot(root);
      REQUIRE(result.ok);
    }
  }

  GIVEN("A pre-rename TSD project manifest")
  {
    // Projects written before the TSD -> VSR rename spell every file ".tsd"
    // and tag it with a __tsd_metadata node carrying a "tsd.*" schema.
    auto writeLegacyMetadata = [](vsr::core::DataNode &root,
                                   const char *fileType,
                                   const char *schema,
                                   int schemaVersion) {
      root.remove(vsr::core::DATA_TREE_METADATA_NODE);
      auto &metadata = root[vsr::core::LEGACY_DATA_TREE_METADATA_NODE];
      metadata["envelopeVersion"] =
          vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION;
      metadata["fileType"] = fileType;
      metadata["schema"] = schema;
      metadata["schemaVersion"] = schemaVersion;
    };
    auto rewriteAsLegacyArchive = [&](const std::filesystem::path &file,
                                      const char *schema) {
      vsr::core::DataTree archive;
      REQUIRE(archive.load(file.string().c_str()));
      writeLegacyMetadata(archive.root(), "scene", schema, 1);
      REQUIRE(archive.save(file.string().c_str()));
    };

    vsr::core::DataTree tree;
    writeLegacyMetadata(tree.root(),
        PROJECT_FILE_TYPE,
        "tsd.scivis-studio.project",
        SCHEMA_VERSION);
    REQUIRE(
        tree.save((root / LEGACY_PROJECT_MANIFEST_FILENAME).string().c_str()));

    std::filesystem::create_directories(root / "scene");
    vsr::scene::Scene scene;
    REQUIRE(vsr::io::save_CameraArchive(
        scene, (root / "scene/cameras.tsd").string().c_str()));
    REQUIRE(vsr::io::save_RendererArchive(
        scene, (root / "scene/renderers.tsd").string().c_str()));
    rewriteAsLegacyArchive(root / "scene/cameras.tsd", "tsd.scene.cameras");
    rewriteAsLegacyArchive(root / "scene/renderers.tsd", "tsd.scene.renderers");

    THEN("Validation succeeds against the legacy filenames and metadata")
    {
      auto result = validateProjectRoot(root);
      REQUIRE(result.ok);
      REQUIRE(
          result.manifestPath.filename() == LEGACY_PROJECT_MANIFEST_FILENAME);
    }
  }

  GIVEN("An invalid metadata schema")
  {
    vsr::core::DataTree tree;
    vsr::core::writeDataTreeMetadata(tree.root(),
        {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
            "application-state",
            "vsr.viewer.state",
            1});
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));

    THEN("Validation fails")
    {
      auto result = validateProjectRoot(root);
      REQUIRE_FALSE(result.ok);
    }
  }

  GIVEN("An invalid legacy project kind")
  {
    vsr::core::DataTree tree;
    tree.root()["projectKind"] = "Other";
    tree.root()["schemaVersion"] = SCHEMA_VERSION;
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));

    THEN("Validation fails")
    {
      auto result = validateProjectRoot(root);
      REQUIRE_FALSE(result.ok);
    }
  }

  GIVEN("A future metadata-tagged project manifest")
  {
    vsr::core::DataTree tree;
    vsr::core::writeDataTreeMetadata(tree.root(),
        {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
            PROJECT_FILE_TYPE,
            PROJECT_SCHEMA,
            SCHEMA_VERSION + 1});
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));

    THEN("Validation fails")
    {
      auto result = validateProjectRoot(root);
      REQUIRE_FALSE(result.ok);
    }
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio requires valid scene pool Archives", "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_required_scene_pools";
  std::filesystem::remove_all(root);

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  REQUIRE(projectContext.saveProject(root));

  std::filesystem::remove(root / "scene/cameras.vsr");
  auto validation = validateProjectRoot(root);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("cameras.vsr") != std::string::npos);

  std::string error;
  REQUIRE_FALSE(projectContext.openProject(root, nullptr, &error));
  REQUIRE(error.find("cameras.vsr") != std::string::npos);

  REQUIRE(projectContext.saveProject(root));
  {
    std::ofstream corrupt(
        root / "scene/renderers.vsr", std::ios::binary | std::ios::trunc);
    corrupt << "not a Renderer Archive";
  }
  validation = validateProjectRoot(root);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("renderers.vsr") != std::string::npos);
  REQUIRE_FALSE(projectContext.openProject(root, nullptr, &error));
  REQUIRE(error.find("renderers.vsr") != std::string::npos);

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio persists one UI-state tree with the project",
    "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_ui_state";
  std::filesystem::remove_all(root);

  GIVEN("A project saved with a {windows, layout, settings} tree")
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();

    vsr::core::DataTree uiState;
    auto &ui = uiState.root();
    ui["windows"]["Viewport"]["visible"] = std::string("yes");
    ui["layout"] = std::string("[Window][Viewport]\nPos=0,0\n");
    ui["settings"]["fontScale"] = 1.5f;
    // Only the three named children are the project's UI state.
    ui["theme"] = std::string("dark");

    std::string error;
    REQUIRE(projectContext.saveProject(root, &ui, &error));

    THEN("The manifest carries the three children at its root")
    {
      vsr::core::DataTree manifest;
      REQUIRE(
          manifest.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
      auto &m = manifest.root();
      REQUIRE(m.child("windows"));
      REQUIRE(m["windows"]["Viewport"]["visible"].getValueAs<std::string>()
          == "yes");
      REQUIRE(m.child("layout"));
      REQUIRE(m["layout"].getValueAs<std::string>()
          == "[Window][Viewport]\nPos=0,0\n");
      REQUIRE(m.child("settings"));
      REQUIRE(m["settings"]["fontScale"].getValueAs<float>() == 1.5f);
      REQUIRE_FALSE(m.child("theme"));
    }

    THEN("Opening the project hands the tree back, replacing what was there")
    {
      vsr::app::Context reopenedContext;
      ProjectContext reopened(&reopenedContext);
      vsr::core::DataTree out;
      out.root()["stale"] = std::string("gone");
      REQUIRE(reopened.openProject(root, &out.root(), &error));
      auto &o = out.root();
      REQUIRE_FALSE(o.child("stale"));
      REQUIRE(o["windows"]["Viewport"]["visible"].getValueAs<std::string>()
          == "yes");
      REQUIRE(o["layout"].getValueAs<std::string>()
          == "[Window][Viewport]\nPos=0,0\n");
      REQUIRE(o["settings"]["fontScale"].getValueAs<float>() == 1.5f);
      REQUIRE_FALSE(o.child("theme"));
    }
  }

  GIVEN("A project saved with an empty layout and no windows or settings")
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();

    vsr::core::DataTree uiState;
    uiState.root()["layout"] = std::string();

    std::string error;
    REQUIRE(projectContext.saveProject(root, &uiState.root(), &error));

    THEN("The manifest carries none of the UI-state keys")
    {
      vsr::core::DataTree manifest;
      REQUIRE(
          manifest.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
      auto &m = manifest.root();
      REQUIRE_FALSE(m.child("windows"));
      REQUIRE_FALSE(m.child("layout"));
      REQUIRE_FALSE(m.child("settings"));
    }

    THEN("Opening the project hands back an empty tree")
    {
      vsr::core::DataTree out;
      out.root()["stale"] = std::string("gone");
      REQUIRE(projectContext.openProject(root, &out.root(), &error));
      REQUIRE(out.root().numChildren() == 0);
    }
  }

  GIVEN("A project saved without a tree")
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    std::string error;
    REQUIRE(projectContext.saveProject(root, nullptr, &error));

    THEN("The manifest carries none of the UI-state keys")
    {
      vsr::core::DataTree manifest;
      REQUIRE(
          manifest.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
      REQUIRE_FALSE(manifest.root().child("windows"));
      REQUIRE_FALSE(manifest.root().child("layout"));
      REQUIRE_FALSE(manifest.root().child("settings"));
    }
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio default project creation", "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &project = projectContext.project();
  REQUIRE(project.name == "Untitled");
  REQUIRE(project.shots.size() == 1);
  REQUIRE(project.lightRigs.size() == 1);
  REQUIRE(project.lightRigs.front().name == "Default");
  REQUIRE(project.shots.front().lightRigId == project.lightRigs.front().id);
  REQUIRE(project.cameraRigs.size() == 1);
  REQUIRE(project.cameraRigs.front().name == "Default");
  REQUIRE(project.shots.front().cameraRigId == project.cameraRigs.front().id);
  REQUIRE(project.activeShotId == project.shots.front().id);
  REQUIRE(project.dirty == false);
  REQUIRE(appContext.vsr.scene.layer("studio") != nullptr);

  auto *layer = appContext.vsr.scene.layer("studio");
  auto lightRigsRoot = findDirectChild(layer->root(), "lightRigs");
  REQUIRE(lightRigsRoot);
  auto rigRoot = findDirectChild(lightRigsRoot, project.lightRigs.front().id);
  REQUIRE(rigRoot);
  REQUIRE(findDirectChild(rigRoot, "mainLight"));
}

SCENARIO("SciVis Studio new shots use the default light rig", "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  const auto defaultRigId = projectContext.project().lightRigs.front().id;
  const auto defaultCameraRigId =
      projectContext.project().cameraRigs.front().id;
  REQUIRE(projectContext.addShot());
  REQUIRE(project::activeShot(projectContext.project())->lightRigId
      == defaultRigId);
  REQUIRE(project::activeShot(projectContext.project())->cameraRigId
      == defaultCameraRigId);
}

SCENARIO(
    "SciVis Studio cloning a light rig deep-copies lights", "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &project = projectContext.project();
  const auto sourceRigId = project.lightRigs.front().id;
  auto *sourceRig = light_rig::findLightRig(project, sourceRigId);
  REQUIRE(sourceRig != nullptr);

  auto sourceRoot = projectContext.resolveLightRigRoot(*sourceRig);
  REQUIRE(sourceRoot);
  auto sourceLightNode = findDirectChild(sourceRoot, "mainLight");
  REQUIRE(sourceLightNode);
  auto *sourceLight =
      dynamic_cast<vsr::scene::Light *>((*sourceLightNode)->getObject());
  REQUIRE(sourceLight != nullptr);
  sourceLight->setParameter("irradiance", 2.f);

  auto *cloneRig = projectContext.cloneLightRig(sourceRigId);
  REQUIRE(cloneRig != nullptr);
  REQUIRE(project.lightRigs.size() == 2);
  REQUIRE(cloneRig->id != sourceRigId);
  REQUIRE(cloneRig->name == "Default Copy");
  REQUIRE(project.shots.front().lightRigId == sourceRigId);

  auto cloneRoot = projectContext.resolveLightRigRoot(*cloneRig);
  REQUIRE(cloneRoot);
  auto cloneLightNode = findDirectChild(cloneRoot, "mainLight");
  REQUIRE(cloneLightNode);
  auto *cloneLight =
      dynamic_cast<vsr::scene::Light *>((*cloneLightNode)->getObject());
  REQUIRE(cloneLight != nullptr);
  REQUIRE(cloneLight != (*sourceLightNode)->getObject());
  REQUIRE(
      cloneLight->parameterValueAs<float>("irradiance").value() == Approx(2.f));

  cloneLight->setParameter("irradiance", 7.f);
  sourceLight =
      dynamic_cast<vsr::scene::Light *>((*sourceLightNode)->getObject());
  REQUIRE(sourceLight != nullptr);
  REQUIRE(sourceLight->parameterValueAs<float>("irradiance").value()
      == Approx(2.f));
}

SCENARIO("SciVis Studio shot dataset bindings update scene visibility",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &scene = appContext.vsr.scene;
  auto *layer = scene.layer("studio");
  REQUIRE(layer != nullptr);

  auto datasetRoot = scene.insertChildNode(layer->root(), "dataset_0001");
  REQUIRE(datasetRoot);

  auto &project = projectContext.project();
  project.datasets.push_back({"dataset_0001",
      "Dataset",
      DatasetSourceKind::Static,
      "OBJ",
      {},
      DatasetStatus::Available,
      DatasetResidency::Loaded,
      projectContext.refFor("studio", datasetRoot)});

  auto &shot = *project::activeShot(project);
  shot::setDatasetBinding(shot, "dataset_0001", false);

  auto *delegate =
      scene.updateDelegate().emplace<CountingLayerUpdateDelegate>();

  projectContext.applyActiveShot();

  REQUIRE_FALSE((*datasetRoot)->isEnabled());
  REQUIRE(delegate->layerStructureUpdates == 1);
  REQUIRE(delegate->lastLayer == layer);
}

SCENARIO("SciVis Studio dataset binding resolves the dataset group by ID",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &scene = appContext.vsr.scene;
  auto *layer = scene.layer("studio");
  REQUIRE(layer != nullptr);

  auto datasetsRoot = findDirectChild(layer->root(), "datasets");
  REQUIRE(datasetsRoot);
  auto datasetRoot = scene.insertChildNode(datasetsRoot, "dataset_0001");
  auto importedFileRoot = scene.insertChildNode(datasetRoot, "imported.vtp");
  auto partRoot = scene.insertChildNode(importedFileRoot, "part_1");

  auto &project = projectContext.project();
  project.datasets.push_back({"dataset_0001",
      "Dataset",
      DatasetSourceKind::Static,
      "VTP",
      {},
      DatasetStatus::Available,
      DatasetResidency::Loaded,
      projectContext.refFor("studio", partRoot)});

  auto &shot = *project::activeShot(project);
  shot::setDatasetBinding(shot, "dataset_0001", false);

  projectContext.applyActiveShot();

  REQUIRE_FALSE((*datasetRoot)->isEnabled());
  REQUIRE((*importedFileRoot)->isEnabled());
  REQUIRE((*partRoot)->isEnabled());
  REQUIRE(project.datasets.front().rootNode.nodeIndex == datasetRoot.index());
}

SCENARIO("SciVis Studio saved projects rebuild runtime refs from stable IDs",
    "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_runtime_refs";
  std::filesystem::remove_all(root);

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();

    auto &scene = appContext.vsr.scene;
    auto *layer = scene.layer("studio");
    REQUIRE(layer != nullptr);
    auto datasetsRoot = findDirectChild(layer->root(), "datasets");
    REQUIRE(datasetsRoot);
    auto datasetRoot = scene.insertChildNode(datasetsRoot, "dataset_0001");
    scene.insertChildNode(datasetRoot, "imported.vtp");

    auto &project = projectContext.project();
    project.datasets.push_back({"dataset_0001",
        "Dataset",
        DatasetSourceKind::Static,
        "VTP",
        {},
        DatasetStatus::Available,
        DatasetResidency::Loaded,
        projectContext.refFor("studio", datasetRoot)});
    shot::setDatasetBinding(
        *project::activeShot(project), "dataset_0001", false);

    REQUIRE(projectContext.saveProject(root));
  }

  {
    vsr::core::DataTree manifest;
    REQUIRE(manifest.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
    auto metadata = vsr::core::readDataTreeMetadata(manifest.root());
    REQUIRE(metadata.status == vsr::core::DataTreeMetadataReadStatus::Found);
    REQUIRE(metadata.metadata);
    REQUIRE(metadata.metadata->fileType == PROJECT_FILE_TYPE);
    REQUIRE(metadata.metadata->schema == PROJECT_SCHEMA);
    REQUIRE(metadata.metadata->schemaVersion == SCHEMA_VERSION);
    REQUIRE(manifest.root().child("projectKind") == nullptr);
    REQUIRE(manifest.root().child("schemaVersion") == nullptr);

    auto &projectNode = manifest.root()["scivisStudio"];
    REQUIRE(projectNode["datasets"].child(0)->child("rootNode") == nullptr);
    REQUIRE(projectNode["shots"].child(0)->child("lightRigId") != nullptr);
    REQUIRE(projectNode["shots"].child(0)->child("cameraRigId") != nullptr);
    REQUIRE(projectNode["shots"].child(0)->child("cameraRig") == nullptr);
    REQUIRE(projectNode["shots"].child(0)->child("camera") == nullptr);
    REQUIRE(projectNode["lightRigs"].child(0)->child("rootNode") == nullptr);
    // v4: rig value data moved to standalone Archives.
    REQUIRE(projectNode["cameraRigs"].child(0)->child("rig") == nullptr);

    REQUIRE(manifest.root().child("context") == nullptr);

    // Each rig was written as its own portable file.
    const auto cameraName = projectNode["cameraRigs"]
                                .child(0)
                                ->child("name")
                                ->getValueAs<std::string>();
    const auto lightName = projectNode["lightRigs"]
                               .child(0)
                               ->child("name")
                               ->getValueAs<std::string>();
    REQUIRE(std::filesystem::exists(root / "cameras" / (cameraName + ".vsr")));
    REQUIRE(std::filesystem::exists(root / "lights" / (lightName + ".vsr")));
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    REQUIRE(projectContext.project().cameraRigs.size() == 1);
    REQUIRE(projectContext.project().shots.front().cameraRigId
        == projectContext.project().cameraRigs.front().id);

    auto *layer = appContext.vsr.scene.layer("studio");
    REQUIRE(layer != nullptr);
    auto datasetsRoot = findDirectChild(layer->root(), "datasets");
    auto datasetRoot = findDirectChild(datasetsRoot, "dataset_0001");
    REQUIRE(datasetRoot);
    REQUIRE_FALSE((*datasetRoot)->isEnabled());
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio projects store scene pools in required Archives",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_scene_pool_archives";
  std::filesystem::remove_all(root);

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    REQUIRE(projectContext.addShot("Second Shot"));
    projectContext.project().activeShotId =
        projectContext.project().shots.front().id;

    auto renderer =
        appContext.vsr.scene.createRenderer("test_device", "pathtracer");
    renderer->setName("selected renderer");
    renderer->setParameter("pixelSamples", 7);
    auto &renderSettings =
        projectContext.project().shots.front().renderSettings;
    renderSettings.rendererLibrary = "test_device";
    renderSettings.rendererObjectIndex = renderer->index();
    renderSettings.rendererSubtype = "pathtracer";

    REQUIRE(projectContext.saveProject(root));
  }

  {
    vsr::core::DataTree manifest;
    REQUIRE(manifest.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
    auto metadata = vsr::core::readDataTreeMetadata(manifest.root());
    REQUIRE(metadata.found());
    REQUIRE(metadata.metadata->schemaVersion == SCHEMA_VERSION);
    REQUIRE(SCHEMA_VERSION == 8);
    REQUIRE(manifest.root().child("context") == nullptr);

    vsr::core::DataTree cameras;
    vsr::core::DataTree renderers;
    REQUIRE(cameras.load((root / "scene/cameras.vsr").string().c_str()));
    REQUIRE(renderers.load((root / "scene/renderers.vsr").string().c_str()));
    REQUIRE(vsr::io::validate_CameraArchive(cameras.root()).accepted());
    REQUIRE(vsr::io::validate_RendererArchive(renderers.root()).accepted());
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));

    auto &shot = projectContext.project().shots.front();
    REQUIRE(projectContext.project().shots.size() == 2);
    REQUIRE(projectContext.project().activeShotId == shot.id);
    auto *camera = projectContext.resolveShotCamera(shot);
    REQUIRE(camera);
    REQUIRE(camera->name() == shot.id + "_camera");
    REQUIRE(shot.renderSettings.rendererObjectIndex != VSR_INVALID_INDEX);
    auto renderer = appContext.vsr.scene.getObject<vsr::scene::Renderer>(
        shot.renderSettings.rendererObjectIndex);
    REQUIRE(renderer);
    REQUIRE(renderer->name() == "selected renderer");
    REQUIRE(renderer->rendererDeviceName() == "test_device");
    REQUIRE(renderer->parameter("pixelSamples"));
    REQUIRE(renderer->parameter("pixelSamples")->value().getAs<int>() == 7);
    auto &secondShot = projectContext.project().shots.back();
    REQUIRE(projectContext.resolveShotCamera(secondShot));
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio writes empty scene pool Archives", "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_empty_scene_pool_archives";
  std::filesystem::remove_all(root);

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    projectContext.project() = {};
    projectContext.project().name = "Empty Pools";
    appContext.vsr.scene.removeAllObjects();
    appContext.vsr.scene.defaultMaterial();

    REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_CAMERA) == 0);
    REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_RENDERER) == 0);
    REQUIRE(projectContext.saveProject(root));
  }

  vsr::core::DataTree cameras;
  vsr::core::DataTree renderers;
  REQUIRE(cameras.load((root / "scene/cameras.vsr").string().c_str()));
  REQUIRE(renderers.load((root / "scene/renderers.vsr").string().c_str()));
  REQUIRE(vsr::io::validate_CameraArchive(cameras.root()).accepted());
  REQUIRE(vsr::io::validate_RendererArchive(renderers.root()).accepted());
  REQUIRE(cameras.root()["objectDB"].child("camera") == nullptr);
  REQUIRE(renderers.root()["objectDB"].child("renderer") == nullptr);

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  REQUIRE(projectContext.openProject(root));
  REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_CAMERA) == 0);
  REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_RENDERER) == 0);
  REQUIRE(appContext.vsr.scene.layer("studio"));

  std::filesystem::remove_all(root);
}

SCENARIO(
    "SciVis Studio projects round-trip standalone datasets", "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_v5_datasets";
  const auto copy = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_v5_datasets_copy";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(copy);

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    auto &scene = appContext.vsr.scene;
    auto *studio = scene.layer("studio");
    auto datasetsRoot = findDirectChild(studio->root(), "datasets");
    auto datasetRoot = scene.insertChildNode(datasetsRoot, "dataset_0001");

    auto geometry = scene.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::sphere);
    geometry->setName("dataset geometry");
    auto material = scene.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = scene.createSurface("dataset surface", geometry, material);
    scene.insertChildObjectNode(datasetRoot, surface, "surface");

    const float times[] = {0.f, 1.f};
    const float radii[] = {0.5f, 1.5f};
    appContext.vsr.animationMgr.addAnimation("dataset radius")
        .addObjectParameterBinding(
            geometry.data(), "radius", ANARI_FLOAT32, radii, times, 2);

    Dataset dataset;
    dataset.id = "dataset_0001";
    dataset.name = "Example";
    dataset.sourceKind = DatasetSourceKind::Static;
    dataset.importerType = "OBJ";
    dataset.source.sourcePath = "/source/example.obj";
    dataset.status = DatasetStatus::Available;
    dataset.rootNode = projectContext.refFor("studio", datasetRoot);
    project.datasets.push_back(std::move(dataset));
    shot::setDatasetBinding(project.shots.front(), "dataset_0001", false);
    project.markDirty();

    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::exists(root / "datasets" / "Example.vsr"));
  }

  {
    vsr::core::DataTree manifest;
    REQUIRE(manifest.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
    auto metadata = vsr::core::readDataTreeMetadata(manifest.root());
    REQUIRE(metadata.found());
    REQUIRE(metadata.metadata->schemaVersion == SCHEMA_VERSION);
    auto *datasetNode = manifest.root()["scivisStudio"]["datasets"].child(0);
    REQUIRE(datasetNode);
    REQUIRE(datasetNode->child("id"));
    REQUIRE(datasetNode->child("name"));
    REQUIRE(datasetNode->child("sourceKind") == nullptr);
    REQUIRE(datasetNode->child("source") == nullptr);
    REQUIRE(datasetNode->child("sourceFiles") == nullptr);
    REQUIRE(manifest.root().child("context") == nullptr);
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.datasets.size() == 1);
    REQUIRE(project.datasets.front().id == "dataset_0001");
    REQUIRE(project.datasets.front().status == DatasetStatus::Available);
    REQUIRE(
        project.datasets.front().source.sourcePath == "/source/example.obj");
    REQUIRE_FALSE(project.shots.front().datasetBindings.front().enabled);
    REQUIRE(projectContext.resolveDatasetRoot(project.datasets.front()));
    REQUIRE(appContext.vsr.animationMgr.animations().size() == 1);
    REQUIRE(appContext.vsr.animationMgr.animations().front().name()
        == "dataset radius");

    const auto asset = root / "datasets" / "Example.vsr";
    const auto sentinel =
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    std::filesystem::last_write_time(asset, sentinel);
    project.shots.front().name = "Unrelated Shot Edit";
    project.markDirty();
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::last_write_time(asset) == sentinel);
    appContext.vsr.animationMgr.setAnimationTime(0.5f);
    REQUIRE_FALSE(project.datasets.front().dirty);

    auto *geometry = appContext.vsr.animationMgr.animations()
                         .front()
                         .objectParameterBindings()
                         .front()
                         .target();
    REQUIRE(geometry);
    geometry->setParameter("radius", 3.f);
    REQUIRE(project.datasets.front().dirty);
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::last_write_time(asset) != sentinel);

    REQUIRE(projectContext.saveProject(copy));
    REQUIRE(std::filesystem::exists(copy / "datasets" / "Example.vsr"));
    auto copied = validateDatasetAsset(copy / "datasets" / "Example.vsr");
    REQUIRE(copied.ok);
    REQUIRE(copied.dataset.source.sourcePath == "/source/example.obj");
  }
  std::filesystem::remove_all(copy);

  std::filesystem::remove(root / "datasets" / "Example.vsr");
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.datasets.size() == 1);
    REQUIRE(project.datasets.front().status == DatasetStatus::Unavailable);
    REQUIRE(project.shots.front().datasetBindings.size() == 1);
    REQUIRE(project.shots.front().datasetBindings.front().datasetId
        == "dataset_0001");
  }

  std::filesystem::remove_all(root);
}

SCENARIO(
    "SciVis Studio projects persist file-animation source lists in "
    "sibling Source List Files",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_source_list_pair";
  const auto copy = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_source_list_pair_copy";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(copy);

  const std::vector<std::string> paths = {"frames/a.raw", "frames/b.raw"};
  const auto datasetFile = root / "datasets" / "Frames.vsr";
  const auto sourcesFile = root / "datasets" / "Frames.sources";

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    auto &scene = appContext.vsr.scene;
    auto datasetsRoot =
        findDirectChild(scene.layer("studio")->root(), "datasets");
    auto dataset = makeFileAnimationDatasetRuntime(scene,
        appContext.vsr.animationMgr,
        datasetsRoot,
        "dataset_0001",
        "Frames",
        paths);
    dataset.status = DatasetStatus::Available;
    dataset.rootNode = projectContext.refFor(
        "studio", findDirectChild(datasetsRoot, "dataset_0001"));
    project.datasets.push_back(std::move(dataset));
    shot::setDatasetBinding(project.shots.front(), "dataset_0001", true);
    project.markDirty();

    // The ADR 0004 transaction stages the pair together: the plan carries
    // both files, and a failure while installing leaves neither behind.
    {
      struct : AssetTransactionFailureInjector
      {
        bool fail(AssetTransactionPhase phase,
            const std::filesystem::path &,
            std::string &message) override
        {
          if (phase != AssetTransactionPhase::ManifestInstall)
            return false;
          message = "injected manifest failure";
          return true;
        }
      } injector;
      ProjectSaveRequest request(
          project, scene, appContext.vsr.animationMgr, root);
      ProjectSaveResult result;
      REQUIRE(buildProjectSavePlan(request, result));
      const auto hasTarget = [&](const char *target) {
        return std::any_of(result.plan.assets.begin(),
            result.plan.assets.end(),
            [&](const ProjectAssetWrite &write) {
              return write.target == std::filesystem::path(target);
            });
      };
      REQUIRE(hasTarget("datasets/Frames.vsr"));
      REQUIRE(hasTarget("datasets/Frames.sources"));
      AssetTransaction transaction(&injector);
      std::string error;
      REQUIRE_FALSE(transaction.commit(result.plan, &error));
      REQUIRE_FALSE(std::filesystem::exists(datasetFile));
      REQUIRE_FALSE(std::filesystem::exists(sourcesFile));
    }

    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::exists(datasetFile));
    REQUIRE(fileContents(sourcesFile) == "frames/a.raw\nframes/b.raw\n");
    vsr::core::DataTree assetTree;
    REQUIRE(assetTree.load(datasetFile.string().c_str()));
    REQUIRE(assetTree.root()["dataset"].child("sourceFiles") == nullptr);
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.datasets.front().status == DatasetStatus::Available);
    REQUIRE(project.datasets.front().sourceFiles.size() == 2);
    REQUIRE(project.datasets.front().sourceFiles[0].path == "frames/a.raw");
    REQUIRE(project.datasets.front().sourceFiles[0].resolvedPath
        == (root / "datasets" / "frames/a.raw").string());

    // Saves that do not touch the source list never rewrite the sibling, so
    // external edits survive: an unrelated project edit...
    const auto sentinel =
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    std::filesystem::last_write_time(sourcesFile, sentinel);
    project.shots.front().name = "Unrelated Shot Edit";
    project.markDirty();
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::last_write_time(sourcesFile) == sentinel);

    // ...and a save that rewrites a dirty dataset file for unrelated reasons.
    project.datasets.front().dirty = true;
    project.markDirty();
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::last_write_time(sourcesFile) == sentinel);
  }

  {
    // A hand-edited Source List File takes effect on the next Dataset Load.
    std::ofstream out(sourcesFile, std::ios::trunc);
    out << "\n  frames/b.raw\nframes/c.raw\nframes/a.raw\n";
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    const auto &dataset = project.datasets.front();
    REQUIRE(dataset.status == DatasetStatus::Available);
    REQUIRE(dataset.sourceFiles.size() == 3);
    REQUIRE(dataset.sourceFiles[0].path == "frames/b.raw");
    REQUIRE(dataset.sourceFiles[1].path == "frames/c.raw");
    REQUIRE(dataset.sourceFiles[2].path == "frames/a.raw");

    // Save As writes the pair into the new project.
    REQUIRE(projectContext.saveProject(copy));
    REQUIRE(std::filesystem::exists(copy / "datasets" / "Frames.vsr"));
    REQUIRE(fileContents(copy / "datasets" / "Frames.sources")
        == "frames/b.raw\nframes/c.raw\nframes/a.raw\n");
  }
  std::filesystem::remove_all(copy);

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    const auto id = project.datasets.front().id;

    // Renaming the dataset renames the pair. A rename is not a source-list
    // edit, so an external edit made after the load is carried over verbatim
    // rather than clobbered by the in-memory entries.
    {
      std::ofstream out(sourcesFile, std::ios::trunc);
      out << "frames/hand-edited-after-load.raw\n";
    }
    REQUIRE(projectContext.renameDataset(id, "Renamed Frames"));
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::exists(root / "datasets" / "Renamed Frames.vsr"));
    REQUIRE(fileContents(root / "datasets" / "Renamed Frames.sources")
        == "frames/hand-edited-after-load.raw\n");
    REQUIRE_FALSE(std::filesystem::exists(datasetFile));
    REQUIRE_FALSE(std::filesystem::exists(sourcesFile));

    // Dataset Removal deletes the pair with the asset.
    REQUIRE(projectContext.removeDataset(id));
    REQUIRE_FALSE(
        std::filesystem::exists(root / "datasets" / "Renamed Frames.vsr"));
    REQUIRE_FALSE(
        std::filesystem::exists(root / "datasets" / "Renamed Frames.sources"));
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio migrates legacy embedded sourceFiles on explicit save",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_source_list_migration";
  std::filesystem::remove_all(root);

  const std::vector<std::string> paths = {"legacy/a.raw", "/legacy/b.raw"};
  const auto datasetFile = root / "datasets" / "Frames.vsr";
  const auto sourcesFile = root / "datasets" / "Frames.sources";

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    auto &scene = appContext.vsr.scene;
    auto datasetsRoot =
        findDirectChild(scene.layer("studio")->root(), "datasets");
    auto dataset = makeFileAnimationDatasetRuntime(scene,
        appContext.vsr.animationMgr,
        datasetsRoot,
        "dataset_0001",
        "Frames",
        paths);
    dataset.status = DatasetStatus::Available;
    dataset.rootNode = projectContext.refFor(
        "studio", findDirectChild(datasetsRoot, "dataset_0001"));
    project.datasets.push_back(std::move(dataset));
    shot::setDatasetBinding(project.shots.front(), "dataset_0001", true);
    project.markDirty();
    REQUIRE(projectContext.saveProject(root));
  }

  // Rewrite the managed asset into the legacy embedded-sourceFiles format.
  {
    vsr::core::DataTree legacy;
    REQUIRE(legacy.load(datasetFile.string().c_str()));
    auto &files = legacy.root()["dataset"]["sourceFiles"];
    for (const auto &path : paths)
      files.append() = path;
    REQUIRE(legacy.save(datasetFile.string().c_str()));
    std::filesystem::remove(sourcesFile);
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.datasets.front().status == DatasetStatus::Available);
    REQUIRE(project.datasets.front().pendingSourceListMigration);
    REQUIRE_FALSE(project.datasets.front().dirty);
    // Merely opening never migrates.
    REQUIRE_FALSE(std::filesystem::exists(sourcesFile));

    // The next explicit save writes the Source List File verbatim and
    // rewrites the dataset file without paths.
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(fileContents(sourcesFile) == "legacy/a.raw\n/legacy/b.raw\n");
    vsr::core::DataTree migrated;
    REQUIRE(migrated.load(datasetFile.string().c_str()));
    REQUIRE(migrated.root()["dataset"].child("sourceFiles") == nullptr);
    REQUIRE_FALSE(project.datasets.front().pendingSourceListMigration);

    // The migrated pair is now stable across further saves.
    const auto sentinel =
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    std::filesystem::last_write_time(sourcesFile, sentinel);
    std::filesystem::last_write_time(datasetFile, sentinel);
    project.markDirty();
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::last_write_time(sourcesFile) == sentinel);
    REQUIRE(std::filesystem::last_write_time(datasetFile) == sentinel);
  }

  std::filesystem::remove_all(root);
}

SCENARIO(
    "SciVis Studio sources edits rewrite the Source List File as an "
    "external tool",
    "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_sources_edit";
  std::filesystem::remove_all(root);

  const auto datasetFile = root / "datasets" / "Frames.vsr";
  const auto sourcesFile = root / "datasets" / "Frames.sources";

  // A saved project with an Unloaded file-animation dataset.
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    auto &scene = appContext.vsr.scene;
    auto datasetsRoot =
        findDirectChild(scene.layer("studio")->root(), "datasets");
    auto dataset = makeFileAnimationDatasetRuntime(scene,
        appContext.vsr.animationMgr,
        datasetsRoot,
        "dataset_0001",
        "Frames",
        {"frames/a.raw", "frames/b.raw"});
    dataset.status = DatasetStatus::Available;
    dataset.rootNode = projectContext.refFor(
        "studio", findDirectChild(datasetsRoot, "dataset_0001"));
    project.datasets.push_back(std::move(dataset));
    shot::setDatasetBinding(project.shots.front(), "dataset_0001", true);
    project.markDirty();
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(projectContext.unloadDataset("dataset_0001"));
  }

  // The edit rewrites only the Source List File, verbatim, regardless of the
  // dataset's residency; the dataset file is untouched.
  const auto sentinel =
      std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
  std::filesystem::last_write_time(datasetFile, sentinel);
  std::string error;
  REQUIRE(writeDatasetSourceListEdit(datasetFile,
      {"remapped/a.raw", "remapped/b.raw", "  spaced entry  "},
      &error));
  REQUIRE(fileContents(sourcesFile)
      == "remapped/a.raw\nremapped/b.raw\n  spaced entry  \n");
  REQUIRE(std::filesystem::last_write_time(datasetFile) == sentinel);

  // The entries read back as authored, modulo the read-time trim (ADR 0013).
  std::vector<std::string> entries;
  REQUIRE(readDatasetSourceListEntries(datasetFile, entries, &error));
  REQUIRE(entries
      == std::vector<std::string>{
          "remapped/a.raw", "remapped/b.raw", "spaced entry"});

  // An empty edit fails and leaves the pair unchanged.
  REQUIRE_FALSE(writeDatasetSourceListEdit(datasetFile, {}, &error));
  REQUIRE(fileContents(sourcesFile)
      == "remapped/a.raw\nremapped/b.raw\n  spaced entry  \n");

  // A missing dataset file is an error: the pair is addressed through it.
  REQUIRE_FALSE(writeDatasetSourceListEdit(
      root / "datasets" / "Nope.vsr", {"x.raw"}, &error));

  std::filesystem::remove_all(root);
}

SCENARIO(
    "SciVis Studio sources edits migrate legacy embedded source lists "
    "in place",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_sources_edit_migration";
  std::filesystem::remove_all(root);

  const std::vector<std::string> paths = {"legacy/a.raw", "legacy/b.raw"};
  const auto datasetFile = root / "datasets" / "Frames.vsr";
  const auto sourcesFile = root / "datasets" / "Frames.sources";

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    auto &scene = appContext.vsr.scene;
    auto datasetsRoot =
        findDirectChild(scene.layer("studio")->root(), "datasets");
    auto dataset = makeFileAnimationDatasetRuntime(scene,
        appContext.vsr.animationMgr,
        datasetsRoot,
        "dataset_0001",
        "Frames",
        paths);
    dataset.status = DatasetStatus::Available;
    dataset.rootNode = projectContext.refFor(
        "studio", findDirectChild(datasetsRoot, "dataset_0001"));
    project.datasets.push_back(std::move(dataset));
    shot::setDatasetBinding(project.shots.front(), "dataset_0001", true);
    project.markDirty();
    REQUIRE(projectContext.saveProject(root));
  }

  // Rewrite the managed asset into the legacy embedded-sourceFiles format.
  {
    vsr::core::DataTree legacy;
    REQUIRE(legacy.load(datasetFile.string().c_str()));
    auto &files = legacy.root()["dataset"]["sourceFiles"];
    for (const auto &path : paths)
      files.append() = path;
    REQUIRE(legacy.save(datasetFile.string().c_str()));
    std::filesystem::remove(sourcesFile);
  }

  // The legacy entries read back from the embedded list.
  std::vector<std::string> entries;
  std::string error;
  REQUIRE(readDatasetSourceListEntries(datasetFile, entries, &error));
  REQUIRE(entries == paths);

  // An explicit sources edit migrates in place: the Source List File is
  // written and the dataset file is rewritten without the embedded list at
  // datatree level, staged together as the ADR 0004 pair transaction.
  REQUIRE(writeDatasetSourceListEdit(
      datasetFile, {"legacy/a.raw", "legacy/b.raw", "legacy/c.raw"}, &error));
  REQUIRE(fileContents(sourcesFile)
      == "legacy/a.raw\nlegacy/b.raw\nlegacy/c.raw\n");
  vsr::core::DataTree migrated;
  REQUIRE(migrated.load(datasetFile.string().c_str()));
  REQUIRE(migrated.root()["dataset"].child("sourceFiles") == nullptr);
  const auto validation = validateDatasetAsset(datasetFile);
  REQUIRE(validation.ok);
  REQUIRE_FALSE(validation.dataset.pendingSourceListMigration);

  // The migrated pair opens as a valid project asset with the edited list.
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    const auto &dataset = projectContext.project().datasets.front();
    REQUIRE(dataset.status == DatasetStatus::Available);
    REQUIRE(dataset.sourceFiles.size() == 3);
    REQUIRE(dataset.sourceFiles[2].path == "legacy/c.raw");
    REQUIRE_FALSE(dataset.pendingSourceListMigration);
  }

  std::filesystem::remove_all(root);
}

SCENARIO(
    "SciVis Studio declared file-animation datasets materialize on "
    "first load",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_declared_dataset";
  const auto framesDir = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_declared_dataset_frames";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(framesDir);

  // Entries for files that do not exist yet: declaring reads nothing.
  const std::vector<std::string> entries = {
      (framesDir / "a_1x1x1_float32.raw").string(),
      (framesDir / "b_1x1x1_float32.raw").string()};

  DatasetID id;
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();

    auto *dataset = projectContext.addDeclaredFileAnimationDataset(
        "Frames", entries, vsr::io::ImporterType::VOLUME_ANIMATION);
    REQUIRE(dataset);
    id = dataset->id;

    // Declared creation reads no source file and builds no runtime; the
    // dataset records Unloaded residency and mirrors the eager create's shot
    // semantics.
    REQUIRE(dataset->declared);
    REQUIRE(dataset->dirty);
    REQUIRE(dataset->residency == DatasetResidency::Unloaded);
    REQUIRE(dataset->sourceKind == DatasetSourceKind::FileAnimation);
    REQUIRE_FALSE(projectContext.resolveDatasetRoot(*dataset));
    REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_VOLUME) == 0);
    auto *activeShot = project::activeShot(project);
    REQUIRE(activeShot->frameCount == 2);
    REQUIRE(shot::findDatasetBinding(*activeShot, id)->enabled);

    // The first save writes the declared pair: a dataset file without a
    // scene representation and the Source List File verbatim.
    REQUIRE(projectContext.saveProject(root));
    const auto validation =
        validateDatasetAsset(root / "datasets" / "Frames.vsr");
    REQUIRE(validation.ok);
    REQUIRE(validation.dataset.declared);
    REQUIRE(fileContents(root / "datasets" / "Frames.sources")
        == entries[0] + "\n" + entries[1] + "\n");
    vsr::core::DataTree assetTree;
    REQUIRE(
        assetTree.load((root / "datasets" / "Frames.vsr").string().c_str()));
    REQUIRE(assetTree.root()["dataset"].child("sourceFiles") == nullptr);
    REQUIRE_FALSE(project::findDataset(project, id)->dirty);

    // While the entries do not resolve, a load fails and changes nothing
    // except revealing unavailability on this machine.
    std::string error;
    REQUIRE_FALSE(projectContext.loadDataset(id, &error));
    REQUIRE(project::findDataset(project, id)->residency
        == DatasetResidency::Unloaded);
    REQUIRE(project::findDataset(project, id)->status
        == DatasetStatus::Unavailable);
    REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_VOLUME) == 0);
  }

  // "Move to the data machine": the declared entries now resolve.
  std::filesystem::create_directories(framesDir);
  for (const auto &entry : entries) {
    std::ofstream raw(entry, std::ios::binary);
    const float voxel = 1.f;
    raw.write(reinterpret_cast<const char *>(&voxel), sizeof(voxel));
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project::findDataset(project, id)->residency
        == DatasetResidency::Unloaded);

    // Materialization: the first successful Dataset Load builds the runtime
    // from the Source List and marks the dataset dirty. Load itself never
    // writes to disk.
    const auto sourcesFile = root / "datasets" / "Frames.sources";
    const auto sentinel =
        std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
    std::filesystem::last_write_time(sourcesFile, sentinel);
    std::string error;
    REQUIRE(projectContext.loadDataset(id, &error));
    auto *record = project::findDataset(project, id);
    REQUIRE(record->residency == DatasetResidency::Loaded);
    REQUIRE(record->status == DatasetStatus::Available);
    REQUIRE(record->dirty);
    REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_VOLUME) == 1);
    REQUIRE(std::filesystem::last_write_time(sourcesFile) == sentinel);

    // The next save bakes the scene representation into the asset, upgrading
    // it in place; the Source List File is not rewritten.
    REQUIRE(projectContext.saveProject(root));
    const auto validation =
        validateDatasetAsset(root / "datasets" / "Frames.vsr");
    REQUIRE(validation.ok);
    REQUIRE_FALSE(validation.dataset.declared);
    REQUIRE(std::filesystem::last_write_time(sourcesFile) == sentinel);
    REQUIRE_FALSE(project::findDataset(project, id)->dirty);
  }

  {
    // The baked asset now hydrates as an ordinary File Animation Dataset.
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto *record = project::findDataset(projectContext.project(), id);
    REQUIRE(record->status == DatasetStatus::Available);
    REQUIRE(record->residency == DatasetResidency::Loaded);
    REQUIRE(record->sourceFiles.size() == 2);
    REQUIRE_FALSE(record->dirty);
    REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_VOLUME) == 1);
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(framesDir);
}

SCENARIO("SciVis Studio opens legacy projects without rewriting them",
    "[SciVisStudio]")
{
  const auto base = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_legacy_project_versions";
  std::filesystem::remove_all(base);

  for (int version = 1; version <= 5; ++version) {
    const auto root = base / std::to_string(version);
    std::filesystem::create_directories(root);

    Project legacyProject;
    legacyProject.name = "Legacy " + std::to_string(version);
    Shot shot;
    shot.id = "shot_0001";
    shot.name = "Legacy Shot";
    legacyProject.shots.push_back(shot);
    legacyProject.activeShotId = shot.id;

    vsr::app::Context legacyContext;
    auto camera = legacyContext.vsr.scene.createObject<vsr::scene::Camera>(
        vsr::scene::tokens::camera::perspective);
    camera->setName(shot.id + "_camera");

    vsr::core::DataTree tree;
    vsr::core::writeDataTreeMetadata(tree.root(),
        {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
            PROJECT_FILE_TYPE,
            PROJECT_SCHEMA,
            version});
    projectToNode(
        legacyProject, tree.root()["scivisStudio"], ProjectForm::Manifest);
    vsr::app::detail::serializeLegacyApplicationContext(
        legacyContext, tree.root()["context"]);
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));

    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    REQUIRE(projectContext.project().name == legacyProject.name);
    REQUIRE(projectContext.resolveShotCamera(
        projectContext.project().shots.front()));

    vsr::core::DataTree unchanged;
    REQUIRE(
        unchanged.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
    auto metadata = vsr::core::readDataTreeMetadata(unchanged.root());
    REQUIRE(metadata.found());
    REQUIRE(metadata.metadata->schemaVersion == version);
    REQUIRE(unchanged.root().child("context"));
    REQUIRE_FALSE(std::filesystem::exists(root / "scene"));

    if (version == 5) {
      REQUIRE(projectContext.saveProject(root));
      vsr::core::DataTree migrated;
      REQUIRE(
          migrated.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
      metadata = vsr::core::readDataTreeMetadata(migrated.root());
      REQUIRE(metadata.found());
      REQUIRE(metadata.metadata->schemaVersion == SCHEMA_VERSION);
      REQUIRE(migrated.root().child("context") == nullptr);
      REQUIRE(std::filesystem::exists(root / "scene/cameras.vsr"));
      REQUIRE(std::filesystem::exists(root / "scene/renderers.vsr"));
    }
  }

  std::filesystem::remove_all(base);
}

SCENARIO("SciVis Studio extracts embedded v4 datasets only on save",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_v4_dataset_migration";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    auto &scene = appContext.vsr.scene;
    auto datasetsRoot =
        findDirectChild(scene.layer("studio")->root(), "datasets");
    auto datasetRoot = scene.insertChildNode(datasetsRoot, "dataset_0042");
    auto geometry = scene.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::sphere);
    auto material = scene.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = scene.createSurface("legacy surface", geometry, material);
    scene.insertChildObjectNode(datasetRoot, surface, "surface");

    Dataset dataset;
    dataset.id = "dataset_0042";
    dataset.name = "Legacy Dataset";
    dataset.status = DatasetStatus::Available;
    dataset.rootNode = projectContext.refFor("studio", datasetRoot);
    project.datasets.push_back(dataset);
    shot::setDatasetBinding(project.shots.front(), dataset.id, true);

    vsr::core::DataTree tree;
    vsr::core::writeDataTreeMetadata(tree.root(),
        {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
            PROJECT_FILE_TYPE,
            PROJECT_SCHEMA,
            4});
    projectToNode(project, tree.root()["scivisStudio"], ProjectForm::Manifest);
    auto *datasetNode = tree.root()["scivisStudio"]["datasets"].child(0);
    REQUIRE(datasetNode);
    (*datasetNode)["sourceKind"] = "Static";
    (*datasetNode)["importerType"] = "OBJ";
    (*datasetNode)["status"] = "Available";
    (*datasetNode)["source"]["absolutePath"] = "/legacy/source.obj";
    vsr::app::detail::serializeLegacyApplicationContext(
        appContext, tree.root()["context"]);
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &dataset = projectContext.project().datasets.front();
    REQUIRE(dataset.id == "dataset_0042");
    REQUIRE(dataset.pendingExtraction);
    REQUIRE(dataset.status == DatasetStatus::Available);
    REQUIRE(dataset.source.sourcePath == "/legacy/source.obj");
    REQUIRE_FALSE(std::filesystem::exists(root / "datasets"));

    REQUIRE(projectContext.saveProject(root));
    REQUIRE(std::filesystem::exists(root / "datasets" / "Legacy Dataset.vsr"));
  }

  {
    vsr::core::DataTree manifest;
    REQUIRE(manifest.load((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
    auto metadata = vsr::core::readDataTreeMetadata(manifest.root());
    REQUIRE(metadata.found());
    REQUIRE(metadata.metadata->schemaVersion == SCHEMA_VERSION);
    auto *datasetNode = manifest.root()["scivisStudio"]["datasets"].child(0);
    REQUIRE(datasetNode);
    REQUIRE(datasetNode->child("sourceKind") == nullptr);
    REQUIRE(datasetNode->child("source") == nullptr);
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.datasets.front().id == "dataset_0042");
    REQUIRE(project.datasets.front().status == DatasetStatus::Available);
    REQUIRE(project.shots.front().datasetBindings.front().datasetId
        == "dataset_0042");
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio Save As reports unavailable datasets", "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_unavailable_source";
  const auto destination = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_unavailable_destination";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
  std::filesystem::create_directories(root);

  Project project;
  project.name = "Unavailable";
  project.projectDirectory = root;
  Dataset dataset;
  dataset.id = "dataset_0007";
  dataset.name = "Missing Dataset";
  dataset.dirty = false;
  dataset.persistedName = dataset.name;
  project.datasets.push_back(dataset);
  Shot shot;
  shot.id = "shot_0001";
  shot.datasetBindings.push_back({dataset.id, true});
  project.shots.push_back(shot);
  project.activeShotId = shot.id;

  vsr::core::DataTree tree;
  vsr::core::writeDataTreeMetadata(tree.root(),
      {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
          PROJECT_FILE_TYPE,
          PROJECT_SCHEMA,
          5});
  projectToNode(project, tree.root()["scivisStudio"], ProjectForm::Manifest);
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager animations(&scene);
  vsr::io::detail::LegacySceneSerializationOptions options;
  options.animationManager = &animations;
  vsr::io::detail::serializeLegacyScenePayload(
      scene, tree.root()["context"], options);
  REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  REQUIRE(projectContext.openProject(root));
  REQUIRE(projectContext.project().datasets.front().status
      == DatasetStatus::Unavailable);
  REQUIRE(projectContext.saveProject(root));

  std::string error;
  REQUIRE_FALSE(projectContext.saveProject(destination, nullptr, &error));
  REQUIRE(error.find("Save As requires every dataset to be available")
      != std::string::npos);
  REQUIRE(error.find("Missing Dataset") != std::string::npos);
  REQUIRE_FALSE(
      std::filesystem::exists(destination / PROJECT_MANIFEST_FILENAME));

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
}

SCENARIO("SciVis Studio dataset lifecycle workflows preserve asset semantics",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_dataset_lifecycle";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_dataset_lifecycle.obj";
  const auto savedArchive = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_dataset_archive.vsr";
  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  std::filesystem::remove(savedArchive);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto *dataset = projectContext.addStaticDataset(
      "Mesh", source, vsr::io::ImporterType::OBJ);
  REQUIRE(dataset);
  REQUIRE(dataset->status == DatasetStatus::Available);
  const auto originalId = dataset->id;
  REQUIRE(projectContext.saveProject(root));
  REQUIRE(std::filesystem::exists(root / "datasets" / "Mesh.vsr"));

  REQUIRE(projectContext.saveDatasetArchive(originalId, savedArchive));
  auto savedValidation = validateDatasetAsset(savedArchive);
  REQUIRE(savedValidation.ok);
  REQUIRE(savedValidation.dataset.id.empty());

  std::string error;
  auto originalRoot = projectContext.resolveDatasetRoot(*dataset);
  REQUIRE(originalRoot);
  std::filesystem::remove(source);
  REQUIRE_FALSE(projectContext.reimportStaticDataset(originalId, &error));
  REQUIRE(projectContext.resolveDatasetRoot(*dataset) == originalRoot);

  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n";
  }
  REQUIRE(projectContext.reimportStaticDataset(originalId, &error));
  REQUIRE(projectContext.project().datasets.front().id == originalId);

  REQUIRE(projectContext.renameDataset(originalId, "Renamed", &error));
  REQUIRE_FALSE(projectContext.renameDataset(originalId, "bad/name", &error));
  REQUIRE(projectContext.saveProject(root));
  REQUIRE(std::filesystem::exists(root / "datasets" / "Renamed.vsr"));
  REQUIRE_FALSE(std::filesystem::exists(root / "datasets" / "Mesh.vsr"));

  REQUIRE(projectContext.removeDataset(originalId, true, &error));
  REQUIRE(std::filesystem::exists(root / "datasets" / "Renamed.vsr"));
  REQUIRE(projectContext.project().shots.front().datasetBindings.empty());

  // A generic VSR scene in the flat directory is not a dataset candidate.
  vsr::scene::Scene genericScene;
  vsr::io::save_SceneArchive(
      genericScene, (root / "datasets" / "generic.vsr").string().c_str());
  REQUIRE(projectContext.saveProject(root));
  REQUIRE(std::filesystem::exists(root / "datasets" / "Renamed.vsr"));
  REQUIRE(std::filesystem::exists(root / "datasets" / "generic.vsr"));
  auto candidates = projectContext.discoverDatasetCandidates();
  REQUIRE(candidates.size() == 1);
  REQUIRE(candidates.front().proposedName == "Renamed");

  auto *incorporated = projectContext.incorporateDatasetCandidate(
      candidates.front(), "Renamed", &error);
  REQUIRE(incorporated);
  REQUIRE(incorporated->id != originalId);
  REQUIRE_FALSE(incorporated->dirty);
  const auto incorporatedId = incorporated->id;
  REQUIRE(projectContext.removeDataset(incorporatedId, false, &error));
  REQUIRE_FALSE(std::filesystem::exists(root / "datasets" / "Renamed.vsr"));
  REQUIRE(std::filesystem::exists(root / "datasets" / "generic.vsr"));

  auto *loaded = projectContext.loadDatasetArchive(savedArchive, &error);
  REQUIRE(loaded);
  REQUIRE(loaded->id != originalId);
  REQUIRE(loaded->dirty);

  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  std::filesystem::remove(savedArchive);
}

SCENARIO("SciVis Studio treats the file-animation pair as one dataset asset",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_source_list_asset";
  const auto archiveDir = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_source_list_archives";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(archiveDir);
  std::filesystem::create_directories(archiveDir);

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto &project = projectContext.project();
  auto &scene = appContext.vsr.scene;
  auto datasetsRoot =
      findDirectChild(scene.layer("studio")->root(), "datasets");
  auto dataset = makeFileAnimationDatasetRuntime(scene,
      appContext.vsr.animationMgr,
      datasetsRoot,
      "dataset_0001",
      "Frames",
      {"frames/a.raw", "frames/b.raw"});
  dataset.status = DatasetStatus::Available;
  dataset.rootNode = projectContext.refFor(
      "studio", findDirectChild(datasetsRoot, "dataset_0001"));
  project.datasets.push_back(std::move(dataset));
  shot::setDatasetBinding(project.shots.front(), "dataset_0001", true);
  project.markDirty();
  REQUIRE(projectContext.saveProject(root));
  const auto id = project.datasets.front().id;

  // Dataset Archive Save writes the pair.
  const auto archiveFile = archiveDir / "Archived.vsr";
  REQUIRE(projectContext.saveDatasetArchive(id, archiveFile));
  REQUIRE(std::filesystem::exists(archiveFile));
  REQUIRE(fileContents(archiveDir / "Archived.sources")
      == "frames/a.raw\nframes/b.raw\n");

  // Dataset Archive Load incorporates the pair with a fresh identity...
  std::string error;
  auto *incorporated = projectContext.loadDatasetArchive(archiveFile, &error);
  REQUIRE(incorporated);
  REQUIRE(incorporated->sourceFiles.size() == 2);
  REQUIRE(incorporated->sourceFiles[0].resolvedPath
      == (archiveDir / "frames/a.raw").string());
  REQUIRE(projectContext.removeDataset(incorporated->id));

  // ...and fails cleanly without the sibling.
  std::filesystem::remove(archiveDir / "Archived.sources");
  const auto datasetCount = project.datasets.size();
  REQUIRE(projectContext.loadDatasetArchive(archiveFile, &error) == nullptr);
  REQUIRE(error.find("Source List File") != std::string::npos);
  REQUIRE(project.datasets.size() == datasetCount);

  // Dataset Load re-reads the Source List File, so a hand edit made while the
  // dataset was Unloaded takes effect when it is loaded back.
  REQUIRE(projectContext.unloadDataset(id, &error));
  {
    std::ofstream out(root / "datasets" / "Frames.sources", std::ios::trunc);
    out << "frames/z.raw\n";
  }
  REQUIRE(projectContext.loadDataset(id, &error));
  REQUIRE(project::findDataset(project, id)->sourceFiles.size() == 1);
  REQUIRE(
      project::findDataset(project, id)->sourceFiles[0].path == "frames/z.raw");

  // Discovery scans only dataset files, and a file-animation dataset file
  // without its sibling is not a valid Dataset Candidate.
  {
    std::error_code ec;
    std::filesystem::copy_file(
        archiveFile, root / "datasets" / "Orphan.vsr", ec);
    REQUIRE_FALSE(ec);
    std::ofstream stray(root / "datasets" / "Stray.sources");
    stray << "frames/a.raw\n";
  }
  REQUIRE(projectContext.discoverDatasetCandidates().empty());
  {
    std::ofstream sibling(root / "datasets" / "Orphan.sources");
    sibling << "frames/a.raw\n";
  }
  auto candidates = projectContext.discoverDatasetCandidates();
  REQUIRE(candidates.size() == 1);
  REQUIRE(candidates.front().file.filename() == "Orphan.vsr");

  // A missing Source List File makes the dataset Unavailable at open; the
  // project itself still opens.
  std::filesystem::remove(root / "datasets" / "Frames.sources");
  {
    vsr::app::Context reopenedContext;
    ProjectContext reopened(&reopenedContext);
    REQUIRE(reopened.openProject(root));
    REQUIRE(reopened.project().datasets.front().status
        == DatasetStatus::Unavailable);
  }

  // Save As copies an Unloaded dataset's pair without loading it.
  const auto destination = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_source_list_asset_save_as";
  std::filesystem::remove_all(destination);
  {
    std::ofstream out(root / "datasets" / "Frames.sources", std::ios::trunc);
    out << "frames/z.raw\n";
  }
  REQUIRE(projectContext.unloadDataset(id, &error));
  REQUIRE(projectContext.saveProject(destination, nullptr, &error));
  REQUIRE(validateDatasetAsset(destination / "datasets" / "Frames.vsr").ok);
  REQUIRE(fileContents(destination / "datasets" / "Frames.sources")
      == "frames/z.raw\n");

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
  std::filesystem::remove_all(archiveDir);
}

SCENARIO("SciVis Studio unloads a clean dataset without touching its asset",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_dataset_unload";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_dataset_unload.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto *dataset = projectContext.addStaticDataset(
      "Mesh", source, vsr::io::ImporterType::OBJ);
  REQUIRE(dataset);
  const auto datasetId = dataset->id;
  REQUIRE(projectContext.saveProject(root));

  auto &project = projectContext.project();
  auto &record = project.datasets.front();
  REQUIRE_FALSE(record.dirty);
  REQUIRE_FALSE(project.dirty);
  const auto assetFile = root / "datasets" / "Mesh.vsr";
  const auto assetWriteTime = std::filesystem::last_write_time(assetFile);
  const auto geometryCount =
      appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY);
  REQUIRE(geometryCount > 0);

  std::string error;
  REQUIRE(projectContext.unloadDataset(datasetId, &error));

  THEN("The runtime representation is gone but the inventory entry remains")
  {
    REQUIRE(record.residency == DatasetResidency::Unloaded);
    REQUIRE(record.status == DatasetStatus::Available);
    REQUIRE_FALSE(projectContext.resolveDatasetRoot(record));
    REQUIRE(
        appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) < geometryCount);
    REQUIRE(record.id == datasetId);
    REQUIRE_FALSE(record.dirty);
    REQUIRE(shot::findDatasetBinding(*project::activeShot(project), datasetId));
  }

  THEN("Unload marks the project dirty and never writes to disk")
  {
    REQUIRE(project.dirty);
    REQUIRE(std::filesystem::last_write_time(assetFile) == assetWriteTime);
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
}

SCENARIO("SciVis Studio Dataset Load recreates the runtime from the asset",
    "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_dataset_load";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_dataset_load.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto *dataset = projectContext.addStaticDataset(
      "Mesh", source, vsr::io::ImporterType::OBJ);
  REQUIRE(dataset);
  const auto datasetId = dataset->id;
  REQUIRE(projectContext.saveProject(root));

  auto &project = projectContext.project();
  auto &record = project.datasets.front();
  const auto geometryCount =
      appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY);
  const auto savedFrameCount = project::activeShot(project)->frameCount;
  const auto savedCurrentFrame = project::activeShot(project)->currentFrame;

  std::string error;
  REQUIRE(projectContext.unloadDataset(datasetId, &error));
  REQUIRE(projectContext.loadDataset(datasetId, &error));

  THEN("The dataset is resident again with its identity intact")
  {
    REQUIRE(record.residency == DatasetResidency::Loaded);
    REQUIRE(record.status == DatasetStatus::Available);
    REQUIRE(record.id == datasetId);
    REQUIRE(record.name == "Mesh");
    REQUIRE_FALSE(record.dirty);
    auto datasetRoot = projectContext.resolveDatasetRoot(record);
    REQUIRE(datasetRoot);
    REQUIRE((*datasetRoot)->isEnabled());
    REQUIRE(
        appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) == geometryCount);
    REQUIRE(shot::findDatasetBinding(*project::activeShot(project), datasetId));
    REQUIRE(project.dirty);
  }

  THEN("Dataset Load never mutates shot state")
  {
    REQUIRE(project::activeShot(project)->frameCount == savedFrameCount);
    REQUIRE(project::activeShot(project)->currentFrame == savedCurrentFrame);
  }

  THEN("Loading an already-loaded dataset is a no-op success")
  {
    REQUIRE(projectContext.loadDataset(datasetId, &error));
    REQUIRE(
        appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) == geometryCount);
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
}

SCENARIO("SciVis Studio residency guards keep unloaded datasets read-only",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_residency_guards";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_residency_guards.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto *dataset = projectContext.addStaticDataset(
      "Mesh", source, vsr::io::ImporterType::OBJ);
  REQUIRE(dataset);
  const auto datasetId = dataset->id;
  REQUIRE(projectContext.saveProject(root));
  auto &project = projectContext.project();
  auto &record = project.datasets.front();

  GIVEN("A dirty dataset")
  {
    record.dirty = true;

    THEN("Unload refuses rather than discard unsaved changes")
    {
      std::string error;
      REQUIRE_FALSE(projectContext.unloadDataset(datasetId, &error));
      REQUIRE(error.find("save") != std::string::npos);
      REQUIRE(record.residency == DatasetResidency::Loaded);
      REQUIRE(projectContext.resolveDatasetRoot(record));
    }
  }

  GIVEN("A dataset that is importing")
  {
    record.status = DatasetStatus::Importing;

    THEN("Unload refuses")
    {
      std::string error;
      REQUIRE_FALSE(projectContext.unloadDataset(datasetId, &error));
      REQUIRE(record.residency == DatasetResidency::Loaded);
    }
  }

  GIVEN("An unloaded dataset")
  {
    std::string error;
    REQUIRE(projectContext.unloadDataset(datasetId, &error));

    THEN("Operations that touch the asset require loading first")
    {
      REQUIRE_FALSE(projectContext.renameDataset(datasetId, "Other", &error));
      REQUIRE(error.find("load") != std::string::npos);
      REQUIRE(record.name == "Mesh");
      REQUIRE_FALSE(projectContext.reimportStaticDataset(datasetId, &error));
      const auto archive = root / "standalone.vsr";
      REQUIRE_FALSE(
          projectContext.saveDatasetArchive(datasetId, archive, &error));
      REQUIRE_FALSE(std::filesystem::exists(archive));
    }

    THEN("An in-place asset rewrite requires loading first")
    {
      record.dirty = true;
      REQUIRE_FALSE(projectContext.saveProject(root, nullptr, &error));
      REQUIRE(error.find("read-only") != std::string::npos);
    }

    THEN("Shot bindings and Dataset Removal remain available")
    {
      auto *shot = project::activeShot(project);
      shot::setDatasetBinding(*shot, datasetId, false);
      REQUIRE_FALSE(shot::findDatasetBinding(*shot, datasetId)->enabled);

      REQUIRE(projectContext.removeDataset(datasetId, false, &error));
      REQUIRE(project.datasets.empty());
      REQUIRE_FALSE(std::filesystem::exists(root / "datasets" / "Mesh.vsr"));
    }

    THEN("A failed load changes nothing except revealing unavailability")
    {
      std::filesystem::remove(root / "datasets" / "Mesh.vsr");
      const auto objectCount =
          appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY);
      REQUIRE_FALSE(projectContext.loadDataset(datasetId, &error));
      REQUIRE(record.residency == DatasetResidency::Unloaded);
      REQUIRE(record.status == DatasetStatus::Unavailable);
      REQUIRE_FALSE(projectContext.resolveDatasetRoot(record));
      REQUIRE(
          appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) == objectCount);
    }
  }

  GIVEN("A loaded dataset whose asset file has vanished")
  {
    std::filesystem::remove(root / "datasets" / "Mesh.vsr");

    THEN("Unload refuses rather than discard the only copy of the data")
    {
      std::string error;
      REQUIRE_FALSE(projectContext.unloadDataset(datasetId, &error));
      REQUIRE(error.find("missing") != std::string::npos);
      REQUIRE(record.residency == DatasetResidency::Loaded);
      REQUIRE(projectContext.resolveDatasetRoot(record));
    }
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
}

SCENARIO(
    "SciVis Studio dataset residency survives save and open", "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_residency_roundtrip";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_residency_roundtrip.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  DatasetID datasetId;
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto *dataset = projectContext.addStaticDataset(
        "Mesh", source, vsr::io::ImporterType::OBJ);
    REQUIRE(dataset);
    datasetId = dataset->id;
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(projectContext.unloadDataset(datasetId));
    // Saving with an unloaded dataset persists residency without needing the
    // runtime.
    REQUIRE(projectContext.saveProject(root));
    REQUIRE_FALSE(projectContext.project().dirty);
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.datasets.size() == 1);
    auto &record = project.datasets.front();

    THEN("Opening hydrates only resident datasets")
    {
      REQUIRE(record.residency == DatasetResidency::Unloaded);
      REQUIRE(record.status == DatasetStatus::Available);
      REQUIRE_FALSE(record.dirty);
      REQUIRE_FALSE(project.dirty);
      REQUIRE_FALSE(projectContext.resolveDatasetRoot(record));
      REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) == 0);
    }

    THEN("An explicit Dataset Load brings it back into the scene")
    {
      REQUIRE(projectContext.loadDataset(datasetId));
      REQUIRE(record.residency == DatasetResidency::Loaded);
      REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) > 0);
    }
  }

  std::filesystem::remove(root / "datasets" / "Mesh.vsr");
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &record = projectContext.project().datasets.front();

    THEN("A missing asset makes an unloaded dataset definitively Unavailable")
    {
      REQUIRE(record.residency == DatasetResidency::Unloaded);
      REQUIRE(record.status == DatasetStatus::Unavailable);
    }
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
}

SCENARIO(
    "SciVis Studio bookkeeping open round-trips residency without "
    "building runtimes",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_bookkeeping_open";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_bookkeeping_open.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  // A project with a Loaded static dataset, a Loaded file-animation dataset,
  // and an Unloaded static dataset.
  DatasetID loadedId, framesId, unloadedId;
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    auto &scene = appContext.vsr.scene;
    loadedId =
        projectContext
            .addStaticDataset("Mesh A", source, vsr::io::ImporterType::OBJ)
            ->id;
    unloadedId =
        projectContext
            .addStaticDataset("Mesh B", source, vsr::io::ImporterType::OBJ)
            ->id;
    auto datasetsRoot =
        findDirectChild(scene.layer("studio")->root(), "datasets");
    auto frames = makeFileAnimationDatasetRuntime(scene,
        appContext.vsr.animationMgr,
        datasetsRoot,
        "dataset_0003",
        "Frames",
        {"frames/a.raw", "frames/b.raw"});
    framesId = frames.id;
    frames.status = DatasetStatus::Available;
    frames.rootNode = projectContext.refFor(
        "studio", findDirectChild(datasetsRoot, "dataset_0003"));
    project.datasets.push_back(std::move(frames));
    shot::setDatasetBinding(project.shots.front(), framesId, true);
    project.markDirty();
    REQUIRE(projectContext.saveProject(root));
    REQUIRE(projectContext.unloadDataset(unloadedId));
    REQUIRE(projectContext.saveProject(root));
  }

  ProjectOpenOptions bookkeeping;
  bookkeeping.bookkeeping = true;

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    std::string error;
    REQUIRE(projectContext.openProject(root, nullptr, &error, bookkeeping));
    auto &project = projectContext.project();

    // No dataset runtime is built and recorded residency is untouched.
    {
      REQUIRE_FALSE(project.dirty);
      REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) == 0);
      REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_VOLUME) == 0);
      for (auto &dataset : project.datasets) {
        REQUIRE_FALSE(dataset.dirty);
        REQUIRE(dataset.status == DatasetStatus::Available);
        REQUIRE_FALSE(projectContext.resolveDatasetRoot(dataset));
      }
      REQUIRE(project::findDataset(project, loadedId)->residency
          == DatasetResidency::Loaded);
      REQUIRE(project::findDataset(project, framesId)->residency
          == DatasetResidency::Loaded);
      REQUIRE(project::findDataset(project, unloadedId)->residency
          == DatasetResidency::Unloaded);
    }

    // A bookkeeping save round-trips residency and rewrites no asset.
    {
      const auto assetFile = root / "datasets" / "Mesh A.vsr";
      const auto sourcesFile = root / "datasets" / "Frames.sources";
      const auto sentinel =
          std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
      std::filesystem::last_write_time(assetFile, sentinel);
      std::filesystem::last_write_time(sourcesFile, sentinel);
      project.shots.front().name = "Edited Shot";
      project.markDirty();
      REQUIRE(projectContext.saveProject(root));
      REQUIRE(std::filesystem::last_write_time(assetFile) == sentinel);
      REQUIRE(std::filesystem::last_write_time(sourcesFile) == sentinel);
    }
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.shots.front().name == "Edited Shot");
    REQUIRE(project::findDataset(project, loadedId)->residency
        == DatasetResidency::Loaded);
    REQUIRE(project::findDataset(project, unloadedId)->residency
        == DatasetResidency::Unloaded);
    REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) > 0);
  }

  {
    // Renaming under a bookkeeping open rewrites the managed pair by copying
    // it at datatree level; the scene representation and the Source List File
    // carry over verbatim.
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    std::string error;
    REQUIRE(projectContext.openProject(root, nullptr, &error, bookkeeping));
    REQUIRE(projectContext.renameDataset(framesId, "Renamed Frames", &error));
    REQUIRE(projectContext.saveProject(root, nullptr, &error));
    REQUIRE(std::filesystem::exists(root / "datasets" / "Renamed Frames.vsr"));
    REQUIRE(fileContents(root / "datasets" / "Renamed Frames.sources")
        == "frames/a.raw\nframes/b.raw\n");
    REQUIRE_FALSE(std::filesystem::exists(root / "datasets" / "Frames.vsr"));
    REQUIRE_FALSE(
        std::filesystem::exists(root / "datasets" / "Frames.sources"));

    // Residency changes remain available without runtimes: Unload is pure
    // bookkeeping, and Load builds just that dataset's runtime.
    REQUIRE(projectContext.unloadDataset(loadedId, &error));
    REQUIRE(projectContext.loadDataset(unloadedId, &error));
    REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) > 0);
    REQUIRE(projectContext.saveProject(root, nullptr, &error));
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project::findDataset(project, framesId)->name == "Renamed Frames");
    REQUIRE(project::findDataset(project, framesId)->status
        == DatasetStatus::Available);
    REQUIRE(project::findDataset(project, loadedId)->residency
        == DatasetResidency::Unloaded);
    REQUIRE(project::findDataset(project, unloadedId)->residency
        == DatasetResidency::Loaded);
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
}

SCENARIO("SciVis Studio Save As copies unloaded datasets", "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_as_residency";
  const auto destination = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_as_residency_destination";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_as_residency.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto *parked = projectContext.addStaticDataset(
      "Parked", source, vsr::io::ImporterType::OBJ);
  REQUIRE(parked);
  const auto parkedId = parked->id;
  REQUIRE(projectContext.addStaticDataset(
      "Resident", source, vsr::io::ImporterType::OBJ));
  REQUIRE(projectContext.saveProject(root));

  const auto readBytes = [](const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
  };
  const auto sourceArchive = root / "datasets/Parked.vsr";
  const auto sourceBytes = readBytes(sourceArchive);
  REQUIRE_FALSE(sourceBytes.empty());
  REQUIRE(projectContext.unloadDataset(parkedId));

  std::string error;
  REQUIRE(projectContext.saveProject(destination, nullptr, &error));
  REQUIRE(readBytes(destination / "datasets/Parked.vsr") == sourceBytes);
  REQUIRE(validateDatasetAsset(destination / "datasets/Parked.vsr").ok);
  REQUIRE(validateDatasetAsset(destination / "datasets/Resident.vsr").ok);

  vsr::app::Context reopenedAppContext;
  ProjectContext reopened(&reopenedAppContext);
  REQUIRE(reopened.openProject(destination));
  REQUIRE(reopened.project().datasets.size() == 2);
  REQUIRE(
      reopened.project().datasets[0].residency == DatasetResidency::Unloaded);
  REQUIRE(reopened.project().datasets[0].status == DatasetStatus::Available);
  REQUIRE(reopened.project().datasets[1].residency == DatasetResidency::Loaded);
  REQUIRE(reopened.project().datasets[1].status == DatasetStatus::Available);

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
  std::filesystem::remove(source);
}

SCENARIO("SciVis Studio Save As renames colliding unloaded datasets",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_as_unloaded_collision";
  const auto destination = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_as_unloaded_collision_destination";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_as_unloaded_collision.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto *first = projectContext.addStaticDataset(
      "First", source, vsr::io::ImporterType::OBJ);
  REQUIRE(first);
  const auto firstId = first->id;
  auto *second = projectContext.addStaticDataset(
      "Second", source, vsr::io::ImporterType::OBJ);
  REQUIRE(second);
  const auto secondId = second->id;
  REQUIRE(projectContext.saveProject(root));
  REQUIRE(projectContext.unloadDataset(firstId));
  REQUIRE(projectContext.unloadDataset(secondId));

  auto &datasets = projectContext.project().datasets;
  datasets[0].name = "Duplicate";
  datasets[1].name = "Duplicate";
  projectContext.project().markDirty();

  std::string error;
  REQUIRE(projectContext.saveProject(destination, nullptr, &error));
  const auto firstArchive =
      validateDatasetAsset(destination / "datasets/Duplicate.vsr");
  REQUIRE(firstArchive.ok);
  REQUIRE(firstArchive.dataset.name == "Duplicate");
  const auto secondArchive =
      validateDatasetAsset(destination / "datasets/Duplicate (2).vsr");
  REQUIRE(secondArchive.ok);
  REQUIRE(secondArchive.dataset.name == "Duplicate (2)");

  vsr::app::Context reopenedAppContext;
  ProjectContext reopened(&reopenedAppContext);
  REQUIRE(reopened.openProject(destination));
  REQUIRE(reopened.loadDataset(firstId));
  REQUIRE(reopened.loadDataset(secondId));

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
  std::filesystem::remove(source);
}

SCENARIO("SciVis Studio --openUnloaded overrides initial residency",
    "[SciVisStudio]")
{
  GIVEN("The application command line")
  {
    vsr::app::Context appContext;
    std::vector<std::string> args{
        "scivisStudio", "--openUnloaded", "/some/project"};
    appContext.parseCommandLine(args);

    THEN("--openUnloaded is recognized alongside the project directory")
    {
      REQUIRE(appContext.commandLine.openUnloaded);
      REQUIRE(appContext.commandLine.stateFile == "/some/project");
    }
  }

  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_open_unloaded";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_open_unloaded.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    REQUIRE(projectContext.addStaticDataset(
        "Mesh", source, vsr::io::ImporterType::OBJ));
    REQUIRE(projectContext.saveProject(root));
  }

  ProjectOpenOptions openUnloaded;
  openUnloaded.openUnloaded = true;

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    std::string error;
    REQUIRE(projectContext.openProject(root, nullptr, &error, openUnloaded));
    auto &project = projectContext.project();
    auto &record = project.datasets.front();

    THEN("The override changes initial residency and dirties the project")
    {
      REQUIRE(record.residency == DatasetResidency::Unloaded);
      REQUIRE(record.status == DatasetStatus::Available);
      REQUIRE_FALSE(projectContext.resolveDatasetRoot(record));
      REQUIRE(appContext.vsr.scene.numberOfObjects(ANARI_GEOMETRY) == 0);
      REQUIRE(project.dirty);
    }

    // Session residency is the single source of truth: saving persists it.
    REQUIRE(projectContext.saveProject(root));
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    std::string error;
    REQUIRE(projectContext.openProject(root, nullptr, &error, openUnloaded));

    THEN("An override that changes nothing leaves the project clean")
    {
      auto &project = projectContext.project();
      REQUIRE(project.datasets.front().residency == DatasetResidency::Unloaded);
      REQUIRE_FALSE(project.dirty);
    }
  }

  GIVEN("A pre-v5 legacy project with an embedded dataset")
  {
    const auto legacyRoot = std::filesystem::temp_directory_path()
        / "vsr_scivis_studio_open_unloaded_legacy";
    std::filesystem::remove_all(legacyRoot);
    std::filesystem::create_directories(legacyRoot);
    {
      vsr::app::Context legacyContext;
      ProjectContext legacyProject(&legacyContext);
      legacyProject.createUnsavedProject();
      auto &project = legacyProject.project();
      auto &scene = legacyContext.vsr.scene;
      auto datasetsRoot =
          findDirectChild(scene.layer("studio")->root(), "datasets");
      auto datasetRoot = scene.insertChildNode(datasetsRoot, "dataset_0042");
      auto geometry = scene.createObject<vsr::scene::Geometry>(
          vsr::scene::tokens::geometry::sphere);
      auto material = scene.createObject<vsr::scene::Material>(
          vsr::scene::tokens::material::matte);
      auto surface = scene.createSurface("legacy surface", geometry, material);
      scene.insertChildObjectNode(datasetRoot, surface, "surface");
      Dataset dataset;
      dataset.id = "dataset_0042";
      dataset.name = "Legacy Dataset";
      dataset.status = DatasetStatus::Available;
      dataset.rootNode = legacyProject.refFor("studio", datasetRoot);
      project.datasets.push_back(dataset);

      vsr::core::DataTree tree;
      vsr::core::writeDataTreeMetadata(tree.root(),
          {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
              PROJECT_FILE_TYPE,
              PROJECT_SCHEMA,
              4});
      projectToNode(
          project, tree.root()["scivisStudio"], ProjectForm::Manifest);
      auto *datasetNode = tree.root()["scivisStudio"]["datasets"].child(0);
      REQUIRE(datasetNode);
      (*datasetNode)["sourceKind"] = "Static";
      (*datasetNode)["importerType"] = "OBJ";
      (*datasetNode)["status"] = "Available";
      vsr::app::detail::serializeLegacyApplicationContext(
          legacyContext, tree.root()["context"]);
      REQUIRE(
          tree.save((legacyRoot / PROJECT_MANIFEST_FILENAME).string().c_str()));
    }

    THEN("--openUnloaded is ignored and the project remains saveable")
    {
      vsr::app::Context appContext;
      ProjectContext projectContext(&appContext);
      std::string error;
      REQUIRE(projectContext.openProject(
          legacyRoot, nullptr, &error, openUnloaded));
      auto &record = projectContext.project().datasets.front();
      REQUIRE(record.residency == DatasetResidency::Loaded);
      REQUIRE(record.status == DatasetStatus::Available);
      REQUIRE(projectContext.saveProject(legacyRoot));
    }
    std::filesystem::remove_all(legacyRoot);
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
}

SCENARIO(
    "SciVis Studio shot rendering reports why it did not run", "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  GIVEN("An unsaved project")
  {
    THEN("The render refuses with the reason and no frames")
    {
      const auto result = renderActiveShotToFrames(projectContext);
      REQUIRE_FALSE(result.completed);
      REQUIRE_FALSE(result.cancelled);
      REQUIRE(result.error == "Cannot render an unsaved project");
      REQUIRE(result.framesCompleted == 0);
      REQUIRE(result.outputDirectory.empty());
    }
  }
}

SCENARIO("SciVis Studio shot rendering restores the scene when a frame throws",
    "[SciVisStudio]")
{
  // The frame loop needs a real device; builds without helide skip.
  auto library = anari::loadLibrary("helide",
      [](const void *,
          ANARIDevice,
          ANARIObject,
          anari::DataType,
          ANARIStatusSeverity,
          ANARIStatusCode,
          const char *) {});
  if (!library) {
    WARN("helide ANARI library unavailable, skipping the render throw test");
    return;
  }
  anari::unloadLibrary(library);

  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_render_throw";
  std::filesystem::remove_all(root);

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  REQUIRE(projectContext.saveProject(root));
  auto *shot = project::activeShot(projectContext.project());
  REQUIRE(shot);
  shot->frameCount = 3;
  shot->currentFrame = 2;
  shot->playing = true;
  shot->renderSettings.width = 8;
  shot->renderSettings.height = 8;
  shot->renderSettings.samples = 1;
  // A fresh project has no renderer objects; pick one the way the server's
  // bind does, so the render reaches its frame loop.
  auto device = appContext.anari.loadDevice("helide");
  REQUIRE(device);
  const auto renderers =
      appContext.vsr.scene.createStandardRenderers("helide", device);
  REQUIRE_FALSE(renderers.empty());
  shot->renderSettings.rendererLibrary = "helide";
  shot->renderSettings.rendererObjectIndex = renderers.front()->index();
  shot->renderSettings.rendererSubtype = renderers.front()->subtype().str();
  const auto delegates = appContext.vsr.scene.updateDelegate().size();

  GIVEN("A frame hook that throws on the second frame")
  {
    RenderShotProgress progress;
    progress.onFrame = [](int frame, int) {
      if (frame == 1)
        throw std::runtime_error("frame 1 refused to load");
      return true;
    };

    THEN("The throw propagates and the render's scene state is undone")
    {
      REQUIRE_THROWS_WITH(renderActiveShotToFrames(projectContext, &progress),
          "frame 1 refused to load");
      // The render index the scene mirrored into is gone again.
      REQUIRE(appContext.vsr.scene.updateDelegate().size() == delegates);
      // The shot's time and playback state are back where they were.
      REQUIRE(shot->currentFrame == 2);
      REQUIRE(shot->playing);
      // The one frame rendered before the throw is on disk, the next is not.
      const auto frames = root / "renders" / shot->id;
      REQUIRE(std::filesystem::exists(frames / (shot->id + "_0000.png")));
      REQUIRE_FALSE(std::filesystem::exists(frames / (shot->id + "_0001.png")));
    }
  }

  anari::release(device, device);
  appContext.anari.releaseAllDevices();
  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio shot rendering materializes bound datasets",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_render_residency";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_render_residency.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  const auto firstId =
      projectContext
          .addStaticDataset("First", source, vsr::io::ImporterType::OBJ)
          ->id;
  const auto secondId =
      projectContext
          .addStaticDataset("Second", source, vsr::io::ImporterType::OBJ)
          ->id;
  const auto thirdId =
      projectContext
          .addStaticDataset("Third", source, vsr::io::ImporterType::OBJ)
          ->id;
  REQUIRE(projectContext.saveProject(root));
  REQUIRE(projectContext.unloadDataset(secondId));
  REQUIRE(projectContext.unloadDataset(thirdId));
  REQUIRE(projectContext.saveProject(root));

  auto &project = projectContext.project();
  auto *shot = project::activeShot(project);
  shot::setDatasetBinding(*shot, thirdId, false);
  auto *second = project::findDataset(project, secondId);
  auto *third = project::findDataset(project, thirdId);

  GIVEN("A bound, enabled dataset that is unloaded")
  {
    ShotDatasetResidencyRestore restore;
    std::string error;
    REQUIRE(makeShotDatasetsResident(projectContext, *shot, restore, &error));

    THEN("It is materialized for the render and restored afterward")
    {
      REQUIRE(project::findDataset(project, firstId)->residency
          == DatasetResidency::Loaded);
      REQUIRE(second->residency == DatasetResidency::Loaded);
      REQUIRE(second->status == DatasetStatus::Available);
      // A disabled binding is not part of the shot's rendered intent.
      REQUIRE(third->residency == DatasetResidency::Unloaded);
      REQUIRE(restore.loadedForRender == std::vector<DatasetID>{secondId});

      restoreShotDatasetResidency(projectContext, restore);
      REQUIRE(second->residency == DatasetResidency::Unloaded);
      REQUIRE_FALSE(projectContext.resolveDatasetRoot(*second));
      // Temporary render loads never change what a save would persist.
      REQUIRE_FALSE(project.dirty);
    }
  }

  GIVEN("A bound, enabled dataset that cannot be made resident")
  {
    std::filesystem::remove(root / "datasets" / "Second.vsr");

    THEN("Materialization hard-errors up front and restores what it loaded")
    {
      ShotDatasetResidencyRestore restore;
      std::string error;
      REQUIRE_FALSE(
          makeShotDatasetsResident(projectContext, *shot, restore, &error));
      REQUIRE(error.find("Second") != std::string::npos);
      REQUIRE(second->residency == DatasetResidency::Unloaded);
      REQUIRE_FALSE(project.dirty);
    }
  }

  GIVEN("A bound, enabled dataset that is missing from the project")
  {
    const DatasetID missingId = "dataset_missing";
    shot::setDatasetBinding(*shot, missingId, true);

    THEN("Materialization hard-errors up front and restores what it loaded")
    {
      ShotDatasetResidencyRestore restore;
      std::string error;
      REQUIRE_FALSE(
          makeShotDatasetsResident(projectContext, *shot, restore, &error));
      REQUIRE(error
          == "enabled shot binding references a missing dataset: " + missingId);
      REQUIRE(second->residency == DatasetResidency::Unloaded);
      REQUIRE_FALSE(project.dirty);
    }
  }

  GIVEN("A bound, disabled dataset that is missing from the project")
  {
    shot::setDatasetBinding(*shot, "dataset_missing", false);

    THEN("It does not prevent materialization")
    {
      ShotDatasetResidencyRestore restore;
      std::string error;
      REQUIRE(makeShotDatasetsResident(projectContext, *shot, restore, &error));
      REQUIRE(error.empty());
      REQUIRE(second->residency == DatasetResidency::Loaded);

      restoreShotDatasetResidency(projectContext, restore);
      REQUIRE(second->residency == DatasetResidency::Unloaded);
      REQUIRE_FALSE(project.dirty);
    }
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove(source);
}

SCENARIO("SciVis Studio stages every dirty dataset before replacement",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_dataset_staging";
  std::filesystem::remove_all(root);

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto &scene = appContext.vsr.scene;
  auto datasetsRoot =
      findDirectChild(scene.layer("studio")->root(), "datasets");

  std::vector<vsr::scene::Geometry *> geometries;
  for (int i = 0; i < 2; ++i) {
    const auto id = project::makeGeneratedId("dataset", i + 1);
    const auto name = i == 0 ? std::string("First") : std::string("Second");
    auto datasetRoot = scene.insertChildNode(datasetsRoot, id.c_str());
    auto geometry = scene.createObject<vsr::scene::Geometry>(
        vsr::scene::tokens::geometry::sphere);
    auto material = scene.createObject<vsr::scene::Material>(
        vsr::scene::tokens::material::matte);
    auto surface = scene.createSurface(name.c_str(), geometry, material);
    scene.insertChildObjectNode(datasetRoot, surface, "surface");
    geometries.push_back(geometry.data());

    Dataset dataset;
    dataset.id = id;
    dataset.name = name;
    dataset.sourceKind = DatasetSourceKind::Static;
    dataset.importerType = "OBJ";
    dataset.status = DatasetStatus::Available;
    dataset.rootNode = projectContext.refFor("studio", datasetRoot);
    projectContext.project().datasets.push_back(std::move(dataset));
  }
  REQUIRE(projectContext.saveProject(root));
  geometries.clear();
  geometries.push_back(
      static_cast<vsr::scene::Geometry *>(scene.getObject(ANARI_GEOMETRY, 0)));
  geometries.push_back(
      static_cast<vsr::scene::Geometry *>(scene.getObject(ANARI_GEOMETRY, 1)));
  REQUIRE(geometries[0]);
  REQUIRE(geometries[1]);

  const auto firstAsset = root / "datasets" / "First.vsr";
  auto readBytes = [](const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
  };
  const auto before = readBytes(firstAsset);
  REQUIRE_FALSE(before.empty());

  geometries[0]->setParameter("radius", 7.f);
  projectContext.project().datasets[0].dirty = true;
  auto proxy = scene.createArrayProxy(ANARI_FLOAT32, 4);
  geometries[1]->setParameterObject("invalid.proxy", *proxy);
  projectContext.project().datasets[1].dirty = true;

  std::string error;
  REQUIRE_FALSE(projectContext.saveProject(root, nullptr, &error));
  REQUIRE(error.find("Second") != std::string::npos);
  REQUIRE(readBytes(firstAsset) == before);
  for (const auto &entry :
      std::filesystem::directory_iterator(root / "datasets")) {
    REQUIRE(
        entry.path().filename().string().find(".stage-") == std::string::npos);
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio pool Archive failures preserve the previous project",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_pool_archive_rollback";
  std::filesystem::remove_all(root);

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  REQUIRE(projectContext.saveProject(root));

  auto readBytes = [](const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
  };
  const auto manifest = root / PROJECT_MANIFEST_FILENAME;
  const auto cameras = root / "scene/cameras.vsr";
  const auto renderers = root / "scene/renderers.vsr";
  const auto manifestBefore = readBytes(manifest);
  const auto camerasBefore = readBytes(cameras);
  const auto renderersBefore = readBytes(renderers);

  auto camera = appContext.vsr.scene.getObject<vsr::scene::Camera>(0);
  REQUIRE(camera);
  auto geometry = appContext.vsr.scene.createObject<vsr::scene::Geometry>(
      vsr::scene::tokens::geometry::sphere);
  camera->setParameterObject("invalidPoolDependency", *geometry);
  projectContext.project().shots.front().name = "must not be committed";
  projectContext.project().markDirty();

  std::string error;
  REQUIRE_FALSE(projectContext.saveProject(root, nullptr, &error));
  REQUIRE(error.find("Camera pool Archive") != std::string::npos);
  REQUIRE(readBytes(manifest) == manifestBefore);
  REQUIRE(readBytes(cameras) == camerasBefore);
  REQUIRE(readBytes(renderers) == renderersBefore);
  for (const auto &entry :
      std::filesystem::directory_iterator(root / "scene")) {
    REQUIRE(
        entry.path().filename().string().find(".stage-") == std::string::npos);
    REQUIRE(
        entry.path().filename().string().find(".backup-") == std::string::npos);
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio save collisions leave files and live names unchanged",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_collision";
  const auto destination = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_as_collision";
  const auto source = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_save_collision.obj";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
  std::filesystem::remove(source);
  {
    std::ofstream obj(source);
    obj << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  }

  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto *dataset = projectContext.addStaticDataset(
      "Mesh", source, vsr::io::ImporterType::OBJ);
  REQUIRE(dataset);
  REQUIRE(projectContext.saveProject(root));

  auto readBytes = [](const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
  };
  const auto manifest = root / PROJECT_MANIFEST_FILENAME;
  const auto managed = root / "datasets/Mesh.vsr";
  const auto collision = root / "datasets/bad_name.vsr";
  std::filesystem::copy_file(managed, collision);
  const auto manifestBefore = readBytes(manifest);
  const auto managedBefore = readBytes(managed);
  const auto collisionBefore = readBytes(collision);

  dataset = &projectContext.project().datasets.front();
  dataset->name = "bad/name";
  dataset->dirty = true;
  projectContext.project().markDirty();
  std::string error;
  REQUIRE_FALSE(projectContext.saveProject(root, nullptr, &error));
  REQUIRE(error.find("unowned target") != std::string::npos);
  REQUIRE(dataset->name == "bad/name");
  REQUIRE(dataset->dirty);
  REQUIRE(projectContext.project().projectDirectory == root);
  REQUIRE(readBytes(manifest) == manifestBefore);
  REQUIRE(readBytes(managed) == managedBefore);
  REQUIRE(readBytes(collision) == collisionBefore);

  dataset->name = "Mesh";
  std::filesystem::create_directories(destination / "datasets");
  const auto destinationCollision = destination / "datasets/Mesh.vsr";
  std::filesystem::copy_file(managed, destinationCollision);
  const auto destinationBefore = readBytes(destinationCollision);
  error.clear();
  REQUIRE_FALSE(projectContext.saveProject(destination, nullptr, &error));
  REQUIRE(error.find("unowned target") != std::string::npos);
  REQUIRE(projectContext.project().projectDirectory == root);
  REQUIRE(readBytes(destinationCollision) == destinationBefore);
  REQUIRE_FALSE(
      std::filesystem::exists(destination / PROJECT_MANIFEST_FILENAME));

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(destination);
  std::filesystem::remove(source);
}

SCENARIO(
    "SciVis Studio active shot toggles light rig visibility", "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &project = projectContext.project();
  auto &firstShot = project.shots.front();
  auto *defaultRig = light_rig::findLightRig(project, firstShot.lightRigId);
  REQUIRE(defaultRig != nullptr);
  auto defaultRoot = projectContext.resolveLightRigRoot(*defaultRig);
  REQUIRE(defaultRoot);

  auto *secondRig = projectContext.createLightRig("Second");
  REQUIRE(secondRig != nullptr);
  auto secondRoot = projectContext.resolveLightRigRoot(*secondRig);
  REQUIRE(secondRoot);

  projectContext.addShot("Second Shot");
  auto &secondShot = *project::activeShot(project);
  secondShot.lightRigId = secondRig->id;
  projectContext.applyActiveShot();

  REQUIRE_FALSE((*defaultRoot)->isEnabled());
  REQUIRE((*secondRoot)->isEnabled());

  secondShot.lightRigId.clear();
  projectContext.applyActiveShot();
  REQUIRE_FALSE((*defaultRoot)->isEnabled());
  REQUIRE_FALSE((*secondRoot)->isEnabled());

  secondShot.lightRigId = "missing";
  projectContext.applyActiveShot();
  REQUIRE_FALSE((*defaultRoot)->isEnabled());
  REQUIRE_FALSE((*secondRoot)->isEnabled());
}

SCENARIO(
    "SciVis Studio active shot samples assigned camera rig", "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &project = projectContext.project();
  auto &shot = *project::activeShot(project);
  auto *secondRig = projectContext.createCameraRig("Second Camera");
  REQUIRE(secondRig != nullptr);

  CameraKeyframe keyframe;
  keyframe.frame = 5;
  keyframe.manipulator.orbit.lookat = {7.f, 8.f, 9.f};
  keyframe.manipulator.orbit.azeldist = {10.f, 20.f, 12.f};
  keyframe.manipulator.orbit.fixedDist = 12.f;
  secondRig->keyframes.push_back(keyframe);

  shot.cameraRigId = secondRig->id;
  shot.currentFrame = 5;
  appContext.view.manipulator.setConfig({0.f, 0.f, 0.f}, 1.f);
  projectContext.applyActiveShot();

  REQUIRE(appContext.view.manipulator.at().x == Approx(7.f));
  REQUIRE(appContext.view.manipulator.at().y == Approx(8.f));
  REQUIRE(appContext.view.manipulator.at().z == Approx(9.f));
  REQUIRE(appContext.view.manipulator.azel().x == Approx(10.f));
  REQUIRE(appContext.view.manipulator.azel().y == Approx(20.f));
  REQUIRE(appContext.view.manipulator.distance() == Approx(12.f));

  shot.cameraRigId.clear();
  appContext.view.manipulator.setConfig({1.f, 2.f, 3.f}, 4.f);
  projectContext.applyActiveShot();
  REQUIRE(appContext.view.manipulator.at().x == Approx(1.f));
  REQUIRE(appContext.view.manipulator.at().y == Approx(2.f));
  REQUIRE(appContext.view.manipulator.at().z == Approx(3.f));
  REQUIRE(appContext.view.manipulator.distance() == Approx(4.f));
}

SCENARIO("SciVis Studio removing a light rig clears shot references",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &project = projectContext.project();
  const auto rigId = project.lightRigs.front().id;
  auto *rig = light_rig::findLightRig(project, rigId);
  REQUIRE(rig != nullptr);
  auto root = projectContext.resolveLightRigRoot(*rig);
  REQUIRE(root);

  REQUIRE(projectContext.removeLightRig(rigId));
  REQUIRE(project.lightRigs.empty());
  REQUIRE(project.shots.front().lightRigId.empty());
  auto *layer = appContext.vsr.scene.layer("studio");
  auto lightRigsRoot = findDirectChild(layer->root(), "lightRigs");
  REQUIRE_FALSE(findDirectChild(lightRigsRoot, rigId));
}

SCENARIO("SciVis Studio removing a camera rig clears shot references",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &project = projectContext.project();
  const auto rigId = project.cameraRigs.front().id;
  REQUIRE(projectContext.removeCameraRig(rigId));
  REQUIRE(project.cameraRigs.empty());
  REQUIRE(project.shots.front().cameraRigId.empty());
}

SCENARIO("SciVis Studio v1 shot lights migrate to light rigs", "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_v1_migrate";
  std::filesystem::remove_all(root);

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    project.lightRigs.clear();
    project.shots.front().lightRigId.clear();

    auto *layer = appContext.vsr.scene.layer("studio");
    auto shotsRoot = findDirectChild(layer->root(), "shots");
    auto shotRoot = findDirectChild(shotsRoot, project.shots.front().id);
    auto legacyLights =
        appContext.vsr.scene.insertChildNode(shotRoot, "lights");
    auto light = appContext.vsr.scene.createObject<vsr::scene::Light>(
        vsr::scene::tokens::light::directional);
    light->setName("legacyLight");
    appContext.vsr.scene.insertChildObjectNode(
        legacyLights, light, "legacyLight");

    vsr::core::DataTree tree;
    vsr::core::writeDataTreeMetadata(tree.root(),
        {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
            PROJECT_FILE_TYPE,
            PROJECT_SCHEMA,
            1});
    projectToNode(project, tree.root()["scivisStudio"], ProjectForm::Manifest);
    vsr::app::detail::serializeLegacyApplicationContext(
        appContext, tree.root()["context"]);
    std::filesystem::create_directories(root);
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.lightRigs.size() == 1);
    REQUIRE(project.shots.front().lightRigId == project.lightRigs.front().id);

    auto rigRoot =
        projectContext.resolveLightRigRoot(project.lightRigs.front());
    REQUIRE(rigRoot);
    REQUIRE(findDirectChild(rigRoot, "legacyLight"));
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio v2 shot camera rigs migrate to camera rigs",
    "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_v2_migrate";
  std::filesystem::remove_all(root);

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();
    project.cameraRigs.clear();
    project.shots.front().cameraRigId.clear();

    vsr::core::DataTree tree;
    vsr::core::writeDataTreeMetadata(tree.root(),
        {vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION,
            PROJECT_FILE_TYPE,
            PROJECT_SCHEMA,
            2});
    projectToNode(project, tree.root()["scivisStudio"], ProjectForm::Manifest);

    auto *shotNode = tree.root()["scivisStudio"]["shots"].child(0);
    REQUIRE(shotNode != nullptr);
    auto &cameraRig = (*shotNode)["cameraRig"];

    vsr::rendering::CameraPose currentPose;
    currentPose.lookat = {1.f, 2.f, 3.f};
    currentPose.azeldist = {4.f, 5.f, 6.f};
    currentPose.fixedDist = 6.f;
    vsr::app::serialize_CameraPose(currentPose, cameraRig["current"]["orbit"]);

    vsr::rendering::CameraPose keyframePose;
    keyframePose.lookat = {7.f, 8.f, 9.f};
    keyframePose.azeldist = {10.f, 20.f, 30.f};
    keyframePose.fixedDist = 30.f;
    auto &keyframe = cameraRig["keyframes"].append();
    keyframe["frame"] = 11;
    keyframe["name"] = "legacy";
    keyframe["interpolationToNext"] = "Ease Out + In";
    vsr::app::serialize_CameraPose(
        keyframePose, keyframe["manipulator"]["orbit"]);

    vsr::app::detail::serializeLegacyApplicationContext(
        appContext, tree.root()["context"]);
    std::filesystem::create_directories(root);
    REQUIRE(tree.save((root / PROJECT_MANIFEST_FILENAME).string().c_str()));
  }

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();
    REQUIRE(project.cameraRigs.size() == 1);
    REQUIRE(project.shots.front().cameraRigId == project.cameraRigs.front().id);
    REQUIRE(project.cameraRigs.front().name == "Shot 1 Camera");
    REQUIRE(project.cameraRigs.front().current.orbit.lookat.x == Approx(1.f));
    REQUIRE(project.cameraRigs.front().keyframes.size() == 1);
    REQUIRE(project.cameraRigs.front().keyframes.front().frame == 11);
    REQUIRE(project.cameraRigs.front().keyframes.front().name == "legacy");
    REQUIRE(project.cameraRigs.front().keyframes.front().interpolationToNext
        == CameraInterpolation::EaseOutIn);
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio shot time is driven by the animation manager",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &shot = *project::activeShot(projectContext.project());
  shot.frameCount = 24;
  shot.fps = 12.f;
  shot.currentFrame = 4;
  shot.loop = false;
  projectContext.syncAnimationManagerToActiveShot();

  auto &animMgr = appContext.vsr.animationMgr;
  REQUIRE(animMgr.getAnimationTotalFrames() == 24);
  REQUIRE(animMgr.getAnimationFPS() == Approx(12.f));
  REQUIRE(animMgr.getAnimationFrame() == 4);
  REQUIRE_FALSE(animMgr.isLoop());

  animMgr.setAnimationFrame(9);
  REQUIRE(shot.currentFrame == 9);
}

SCENARIO("SciVis Studio ProjectContext counts the Project's revisions",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  GIVEN("a fresh project")
  {
    const auto revision = projectContext.revision();
    const auto shotRevision = projectContext.activeShotRevision();
    REQUIRE(revision > 0); // the creation itself was a mutation

    WHEN("a shot is added")
    {
      REQUIRE(projectContext.addShot("Two"));

      THEN("both the revision and the active-shot revision move")
      {
        REQUIRE(projectContext.revision() > revision);
        REQUIRE(projectContext.activeShotRevision() > shotRevision);
      }
    }

    WHEN("the active shot is selected again")
    {
      REQUIRE(
          projectContext.setActiveShot(projectContext.project().activeShotId));

      THEN("nothing changed, so neither revision moves")
      {
        REQUIRE(projectContext.revision() == revision);
        REQUIRE(projectContext.activeShotRevision() == shotRevision);
      }
    }

    WHEN("a light rig is created")
    {
      REQUIRE(projectContext.createLightRig("Fill"));

      THEN("the revision moves; the active shot is as it was")
      {
        REQUIRE(projectContext.revision() > revision);
        REQUIRE(projectContext.activeShotRevision() == shotRevision);
      }
    }

    WHEN("a shot other than the active one is updated")
    {
      REQUIRE(projectContext.addShot("Two"));
      const auto afterAdd = projectContext.revision();
      const auto shotAfterAdd = projectContext.activeShotRevision();
      Shot first = projectContext.project().shots.front();
      first.name = "Renamed";
      REQUIRE(projectContext.updateShot(first));

      THEN("the revision moves and the active-shot revision does not")
      {
        REQUIRE(projectContext.revision() > afterAdd);
        REQUIRE(projectContext.activeShotRevision() == shotAfterAdd);
      }

      AND_WHEN("the active shot is updated")
      {
        const auto beforeActive = projectContext.activeShotRevision();
        Shot active = *project::activeShot(projectContext.project());
        active.name = "Active renamed";
        REQUIRE(projectContext.updateShot(active));

        THEN("the active-shot revision moves too")
        {
          REQUIRE(projectContext.activeShotRevision() > beforeActive);
        }
      }
    }

    WHEN("a paused shot is sought to another frame")
    {
      auto &shot = *project::activeShot(projectContext.project());
      shot.frameCount = 24;
      projectContext.syncAnimationManagerToActiveShot();
      const auto synced = projectContext.revision();
      projectContext.setActiveShotFrame(9);

      THEN("the frame moved without moving the revision (Time in Motion)")
      {
        REQUIRE(shot.currentFrame == 9);
        REQUIRE(projectContext.revision() == synced);

        AND_WHEN("the resting frame is committed")
        {
          projectContext.markRevised();

          THEN("the revision moves once")
          {
            REQUIRE(projectContext.revision() == synced + 1);
          }
        }
      }
    }

    WHEN("a non-looping shot plays off its end")
    {
      auto &shot = *project::activeShot(projectContext.project());
      shot.frameCount = 4;
      shot.fps = 10.f;
      shot.loop = false;
      projectContext.syncAnimationManagerToActiveShot();
      const auto synced = projectContext.revision();
      REQUIRE(projectContext.setPlaying(shot.id, true));
      const auto playing = projectContext.revision();
      REQUIRE(playing > synced);
      // Asking for the state the shot is in is a no-op: nothing moves.
      REQUIRE(projectContext.setPlaying(shot.id, true));
      REQUIRE(projectContext.revision() == playing);

      // One frame's worth of time: the shot advances but keeps playing.
      appContext.vsr.animationMgr.tick(0.1f);
      REQUIRE(shot.playing);
      REQUIRE(shot.currentFrame == 1);

      THEN("the per-frame tick does not move the revision")
      {
        REQUIRE(projectContext.revision() == playing);
      }

      AND_WHEN("the ticks carry it past the last frame")
      {
        for (int i = 0; i < 8 && shot.playing; ++i)
          appContext.vsr.animationMgr.tick(0.1f);
        REQUIRE_FALSE(shot.playing);

        THEN("the auto-stop is one mutation")
        {
          REQUIRE(projectContext.revision() == playing + 1);
        }
      }
    }
  }
}

SCENARIO("SciVis Studio CLI parses noun-verb command lines", "[SciVisStudio]")
{
  StudioCommandLine commandLine;
  std::string error;

  REQUIRE(parseStudioCommandLine(
      {"scivisStudioCLI", "project", "init", "/tmp/project", "--name", "P"},
      commandLine,
      error));
  REQUIRE(commandLine.command == StudioCommand::ProjectInit);
  REQUIRE(
      commandLine.projectDirectory == std::filesystem::path("/tmp/project"));
  REQUIRE(commandLine.name == "P");

  REQUIRE(parseStudioCommandLine(
      {"scivisStudioCLI", "dataset", "list", "/tmp/project"},
      commandLine,
      error));
  REQUIRE(commandLine.command == StudioCommand::DatasetList);

  REQUIRE(parseStudioCommandLine({"scivisStudioCLI",
                                     "dataset",
                                     "create",
                                     "file-animation",
                                     "/tmp/project",
                                     "--importer",
                                     "VOLUME_ANIMATION",
                                     "--declare",
                                     "--no-shot-frame-count",
                                     "a.raw",
                                     "b.raw"},
      commandLine,
      error));
  REQUIRE(commandLine.command == StudioCommand::DatasetCreateFileAnimation);
  REQUIRE(commandLine.declare);
  REQUIRE_FALSE(commandLine.setShotFrameCount);
  REQUIRE(commandLine.importerType == "VOLUME_ANIMATION");
  REQUIRE(commandLine.paths == std::vector<std::string>{"a.raw", "b.raw"});

  REQUIRE(parseStudioCommandLine({"scivisStudioCLI",
                                     "dataset",
                                     "create",
                                     "static",
                                     "/tmp/project",
                                     "--importer",
                                     "OBJ",
                                     "mesh.obj"},
      commandLine,
      error));
  REQUIRE(commandLine.command == StudioCommand::DatasetCreateStatic);
  REQUIRE(commandLine.paths == std::vector<std::string>{"mesh.obj"});

  REQUIRE(parseStudioCommandLine({"scivisStudioCLI",
                                     "dataset",
                                     "sources",
                                     "remap",
                                     "/tmp/project",
                                     "Frames",
                                     "--from",
                                     "/old",
                                     "--to",
                                     "/new"},
      commandLine,
      error));
  REQUIRE(commandLine.command == StudioCommand::DatasetSourcesRemap);
  REQUIRE(commandLine.dataset == "Frames");
  REQUIRE(*commandLine.remapFrom == "/old");
  REQUIRE(*commandLine.remapTo == "/new");

  REQUIRE(parseStudioCommandLine({"scivisStudioCLI",
                                     "dataset",
                                     "rename",
                                     "/tmp/project",
                                     "dataset_0001",
                                     "New Name"},
      commandLine,
      error));
  REQUIRE(commandLine.command == StudioCommand::DatasetRename);
  REQUIRE(commandLine.dataset == "dataset_0001");
  REQUIRE(commandLine.name == "New Name");

  REQUIRE(parseStudioCommandLine({"scivisStudioCLI",
                                     "dataset",
                                     "remove",
                                     "/tmp/project",
                                     "dataset_0001",
                                     "--keep-asset"},
      commandLine,
      error));
  REQUIRE(commandLine.command == StudioCommand::DatasetRemove);
  REQUIRE(commandLine.keepAsset);

  REQUIRE(parseStudioCommandLine(
      {"scivisStudioCLI", "--help"}, commandLine, error));
  REQUIRE(commandLine.showHelp);

  // Grammar violations are parse errors, not surprises at run time.
  REQUIRE_FALSE(parseStudioCommandLine(
      {"scivisStudioCLI", "shot", "list", "/tmp/project"}, commandLine, error));
  REQUIRE(error.find("unknown command") != std::string::npos);
  REQUIRE_FALSE(parseStudioCommandLine(
      {"scivisStudioCLI", "dataset", "show"}, commandLine, error));
  REQUIRE(error.find("missing") != std::string::npos);
  REQUIRE_FALSE(parseStudioCommandLine({"scivisStudioCLI",
                                           "dataset",
                                           "remove",
                                           "/tmp/project",
                                           "dataset_0001",
                                           "--declare"},
      commandLine,
      error));
  REQUIRE(error.find("--declare") != std::string::npos);
  REQUIRE_FALSE(parseStudioCommandLine({"scivisStudioCLI",
                                           "dataset",
                                           "sources",
                                           "remap",
                                           "/tmp/project",
                                           "Frames",
                                           "--from",
                                           "/old"},
      commandLine,
      error));
  REQUIRE(error.find("--from and --to") != std::string::npos);
  REQUIRE_FALSE(parseStudioCommandLine(
      {"scivisStudioCLI", "dataset", "create", "static", "/tmp/project"},
      commandLine,
      error));
  REQUIRE(error.find("source path") != std::string::npos);
}

SCENARIO("SciVis Studio CLI addresses datasets by ID then unique name",
    "[SciVisStudio]")
{
  Project project;
  project.datasets.push_back({"dataset_0001", "Frames"});
  project.datasets.push_back({"dataset_0002", "frames"});
  project.datasets.push_back({"dataset_0003", "Other"});

  std::string error;
  REQUIRE(resolveDatasetSelector(project, "dataset_0002", error)
      == &project.datasets[1]);
  REQUIRE(
      resolveDatasetSelector(project, "other", error) == &project.datasets[2]);

  // Exact ID wins before names are considered; a name matching several
  // datasets case-insensitively is ambiguous and lists the candidates.
  REQUIRE(resolveDatasetSelector(project, "FRAMES", error) == nullptr);
  REQUIRE(error.find("multiple") != std::string::npos);
  REQUIRE(error.find("dataset_0001") != std::string::npos);
  REQUIRE(error.find("dataset_0002") != std::string::npos);

  REQUIRE(resolveDatasetSelector(project, "missing", error) == nullptr);
  REQUIRE(error.find("no dataset matches") != std::string::npos);
  REQUIRE(error.find("dataset_0003") != std::string::npos);
}

SCENARIO("SciVis Studio CLI gathers source-list entries and remaps prefixes",
    "[SciVisStudio]")
{
  StudioCommandLine commandLine;
  std::vector<std::string> entries;
  std::string error;

  // Positional entries win.
  commandLine.paths = {"a.raw", "b.raw"};
  std::istringstream unusedInput("ignored.raw\n");
  REQUIRE(gatherSourceListEntries(commandLine, unusedInput, entries, error));
  REQUIRE(entries == std::vector<std::string>{"a.raw", "b.raw"});

  // stdin entries follow Source List File rules: trimmed, blanks skipped.
  commandLine.paths.clear();
  std::istringstream input("\n  first.raw  \n\nsecond.raw\n");
  REQUIRE(gatherSourceListEntries(commandLine, input, entries, error));
  REQUIRE(entries == std::vector<std::string>{"first.raw", "second.raw"});

  std::istringstream emptyInput;
  REQUIRE_FALSE(
      gatherSourceListEntries(commandLine, emptyInput, entries, error));

  // --paths-from FILE reads the same one-entry-per-line format.
  const auto listFile = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_cli_paths_from.txt";
  {
    std::ofstream out(listFile, std::ios::trunc);
    out << "x.raw\n\n  y.raw\n";
  }
  commandLine.pathsFrom = listFile;
  std::istringstream unread;
  REQUIRE(gatherSourceListEntries(commandLine, unread, entries, error));
  REQUIRE(entries == std::vector<std::string>{"x.raw", "y.raw"});
  std::filesystem::remove(listFile);

  // Remap is a literal prefix substitution on raw entries.
  entries = {"/old/a.raw", "/older/b.raw", "relative/c.raw"};
  REQUIRE(remapSourceListEntries(entries, "/old", "/new") == 2);
  REQUIRE(entries
      == std::vector<std::string>{
          "/new/a.raw", "/newer/b.raw", "relative/c.raw"});
}

SCENARIO("SciVis Studio CLI drives a headless declared-dataset round trip",
    "[SciVisStudio]")
{
  const auto root = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_cli_round_trip";
  const auto framesDir = std::filesystem::temp_directory_path()
      / "vsr_scivis_studio_cli_round_trip_frames";
  std::filesystem::remove_all(root);
  std::filesystem::remove_all(framesDir);

  const auto run = [](std::vector<std::string> argv,
                       const std::string &in = std::string()) {
    argv.insert(argv.begin(), "scivisStudioCLI");
    StudioCommandLine commandLine;
    std::string error;
    REQUIRE(parseStudioCommandLine(argv, commandLine, error));
    std::istringstream input(in);
    std::ostringstream output;
    const int exitCode = runStudioCommand(commandLine, input, output);
    return std::make_pair(exitCode, output.str());
  };

  // project init mirrors createUnsavedProject: default shot + default rigs.
  auto [initCode, initOut] =
      run({"project", "init", root.string(), "--name", "CLI Project"});
  REQUIRE(initCode == 0);
  REQUIRE(initOut.find("CLI Project") != std::string::npos);
  REQUIRE(std::filesystem::exists(root / "project.vsr"));

  // Declare a dataset for another "machine", entries via stdin.
  auto [declareCode, declareOut] = run({"dataset",
                                           "create",
                                           "file-animation",
                                           root.string(),
                                           "--importer",
                                           "VOLUME_ANIMATION",
                                           "--declare",
                                           "--name",
                                           "Frames"},
      "/authoring/a_1x1x1_float32.raw\n/authoring/b_1x1x1_float32.raw\n");
  REQUIRE(declareCode == 0);
  REQUIRE(declareOut.find("declared") != std::string::npos);
  REQUIRE(fileContents(root / "datasets" / "Frames.sources")
      == "/authoring/a_1x1x1_float32.raw\n/authoring/b_1x1x1_float32.raw\n");
  REQUIRE(
      validateDatasetAsset(root / "datasets" / "Frames.vsr").dataset.declared);

  auto [listCode, listOut] = run({"dataset", "list", root.string()});
  REQUIRE(listCode == 0);
  REQUIRE(listOut.find("Frames") != std::string::npos);
  REQUIRE(listOut.find("Unloaded") != std::string::npos);

  // On the "data machine" the files live elsewhere: remap the prefix.
  auto [remapCode, remapOut] = run({"dataset",
      "sources",
      "remap",
      root.string(),
      "Frames",
      "--from",
      "/authoring",
      "--to",
      framesDir.string()});
  REQUIRE(remapCode == 0);
  REQUIRE(remapOut.find("2 remapped") != std::string::npos);

  auto [showCode, showOut] = run({"dataset", "show", root.string(), "Frames"});
  REQUIRE(showCode == 0);
  REQUIRE(showOut.find((framesDir / "a_1x1x1_float32.raw").string())
      != std::string::npos);

  // Loading before the files exist fails and leaves the project untouched.
  auto [failCode, failOut] = run({"dataset", "load", root.string(), "Frames"});
  REQUIRE(failCode != 0);
  REQUIRE(
      validateDatasetAsset(root / "datasets" / "Frames.vsr").dataset.declared);

  // Materialize: with the files in place, dataset load imports and the save
  // bakes the scene representation into the asset.
  std::filesystem::create_directories(framesDir);
  for (const auto *name : {"a_1x1x1_float32.raw", "b_1x1x1_float32.raw"}) {
    std::ofstream raw(framesDir / name, std::ios::binary);
    const float voxel = 1.f;
    raw.write(reinterpret_cast<const char *>(&voxel), sizeof(voxel));
  }
  auto [loadCode, loadOut] = run({"dataset", "load", root.string(), "Frames"});
  REQUIRE(loadCode == 0);
  REQUIRE(loadOut.find("materialized") != std::string::npos);
  {
    const auto validation =
        validateDatasetAsset(root / "datasets" / "Frames.vsr");
    REQUIRE(validation.ok);
    REQUIRE_FALSE(validation.dataset.declared);
  }

  // Loading again is a no-op success.
  auto [reloadCode, reloadOut] =
      run({"dataset", "load", root.string(), "Frames"});
  REQUIRE(reloadCode == 0);
  REQUIRE(reloadOut.find("already Loaded") != std::string::npos);

  // Rename renames the pair; unload is pure bookkeeping.
  auto [renameCode, renameOut] =
      run({"dataset", "rename", root.string(), "Frames", "Sim Frames"});
  REQUIRE(renameCode == 0);
  REQUIRE(std::filesystem::exists(root / "datasets" / "Sim Frames.vsr"));
  REQUIRE(std::filesystem::exists(root / "datasets" / "Sim Frames.sources"));
  REQUIRE_FALSE(std::filesystem::exists(root / "datasets" / "Frames.vsr"));

  auto [unloadCode, unloadOut] =
      run({"dataset", "unload", root.string(), "Sim Frames"});
  REQUIRE(unloadCode == 0);

  auto [projectShowCode, projectShowOut] =
      run({"project", "show", root.string()});
  REQUIRE(projectShowCode == 0);
  REQUIRE(projectShowOut.find("Sim Frames") != std::string::npos);
  REQUIRE(projectShowOut.find("Unloaded") != std::string::npos);

  // Removal with --keep-asset persists the inventory change and leaves the
  // pair on disk.
  auto [removeCode, removeOut] =
      run({"dataset", "remove", root.string(), "Sim Frames", "--keep-asset"});
  REQUIRE(removeCode == 0);
  REQUIRE(std::filesystem::exists(root / "datasets" / "Sim Frames.vsr"));
  REQUIRE(std::filesystem::exists(root / "datasets" / "Sim Frames.sources"));
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    REQUIRE(projectContext.project().datasets.empty());
  }

  std::filesystem::remove_all(root);
  std::filesystem::remove_all(framesDir);
}

SCENARIO("SciVis Studio render-shot CLI parses command line", "[SciVisStudio]")
{
  RenderShotCommandLine commandLine;
  std::string error;

  REQUIRE(parseRenderShotCommandLine(
      {"scivisStudioRenderShot", "/tmp/project", "--shot", "shot_0002"},
      commandLine,
      error));
  REQUIRE(
      commandLine.projectDirectory == std::filesystem::path("/tmp/project"));
  REQUIRE(commandLine.shotId == "shot_0002");
  REQUIRE_FALSE(commandLine.showHelp);

  REQUIRE(parseRenderShotCommandLine(
      {"scivisStudioRenderShot", "--help"}, commandLine, error));
  REQUIRE(commandLine.showHelp);

  REQUIRE_FALSE(parseRenderShotCommandLine(
      {"scivisStudioRenderShot", "/tmp/project", "--shot"},
      commandLine,
      error));
  REQUIRE(error.find("--shot requires") != std::string::npos);
}

SCENARIO("SciVis Studio render-shot CLI selects shots", "[SciVisStudio]")
{
  Project project;
  project.shots.push_back({"shot_0001", "Overview"});
  project.shots.push_back({"shot_0002", "Detail"});

  std::string error;
  std::istringstream emptyInput;
  std::ostringstream output;

  auto *shot = selectShotForRender(
      project, "shot_0002", false, emptyInput, output, error);
  REQUIRE(shot != nullptr);
  REQUIRE(shot->id == "shot_0002");

  shot =
      selectShotForRender(project, "missing", false, emptyInput, output, error);
  REQUIRE(shot == nullptr);
  REQUIRE(error.find("unknown shot ID: missing") != std::string::npos);
  REQUIRE(error.find("shot_0001") != std::string::npos);

  shot = selectShotForRender(project, "", false, emptyInput, output, error);
  REQUIRE(shot == nullptr);
  REQUIRE(error.find("multiple shots found") != std::string::npos);
  REQUIRE(error.find("--shot <shot-id>") != std::string::npos);

  std::istringstream selectionInput("2\n");
  output.str("");
  output.clear();
  shot = selectShotForRender(project, "", true, selectionInput, output, error);
  REQUIRE(shot != nullptr);
  REQUIRE(shot->id == "shot_0002");
  REQUIRE(output.str().find("Select shot [1-2]") != std::string::npos);

  std::istringstream invalidInput("3\n");
  shot = selectShotForRender(project, "", true, invalidInput, output, error);
  REQUIRE(shot == nullptr);
  REQUIRE(error.find("invalid shot selection: 3") != std::string::npos);
}

SCENARIO(
    "SciVis Studio render-shot CLI auto-selects one shot", "[SciVisStudio]")
{
  Project project;
  project.shots.push_back({"shot_0001", "Only Shot"});

  std::string error;
  std::istringstream input;
  std::ostringstream output;
  auto *shot = selectShotForRender(project, "", false, input, output, error);
  REQUIRE(shot != nullptr);
  REQUIRE(shot->id == "shot_0001");
}

SCENARIO("SciVis Studio rig name validation", "[SciVisStudio]")
{
  GIVEN("Rig name format rules")
  {
    std::string error;
    REQUIRE(validateRigName("Key Light", &error));
    REQUIRE(validateRigName("rig-01 Copy", &error));
    REQUIRE(validateRigName("Default Copy", &error));

    REQUIRE_FALSE(validateRigName("", &error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(validateRigName("bad/name", &error));
    REQUIRE_FALSE(validateRigName("path\\sep", &error));
    REQUIRE_FALSE(validateRigName(" leading", &error));
    REQUIRE_FALSE(validateRigName("trailing ", &error));
    REQUIRE_FALSE(validateRigName(".", &error));
    REQUIRE_FALSE(validateRigName("with*star", &error));
  }

  GIVEN("Sanitization of arbitrary strings")
  {
    REQUIRE(sanitizeRigName("a/b*c") == "a_b_c");
    REQUIRE(sanitizeRigName("  spaced  ") == "spaced");
    REQUIRE(validateRigName(sanitizeRigName("weird:name?")));
    REQUIRE(sanitizeRigName("") == "rig");
    REQUIRE(sanitizeRigName("   ") == "rig");
    REQUIRE(validateRigName(sanitizeRigName("///")));
  }
}

SCENARIO(
    "SciVis Studio rig rename enforces format and uniqueness", "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto &project = projectContext.project();
  auto *second = projectContext.createLightRig("Second");
  REQUIRE(second != nullptr);
  const auto defaultId = project.lightRigs.front().id;
  const auto secondId = second->id;

  std::string error;
  WHEN("renaming to a valid, unused name")
  {
    REQUIRE(projectContext.renameLightRig(defaultId, "Studio Key", &error));
    REQUIRE(light_rig::findLightRig(project, defaultId)->name == "Studio Key");
  }

  WHEN("renaming to a name used by another rig (case-insensitive)")
  {
    REQUIRE_FALSE(projectContext.renameLightRig(defaultId, "second", &error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(light_rig::findLightRig(project, defaultId)->name == "Default");
  }

  WHEN("renaming a rig to its own current name")
  {
    REQUIRE(projectContext.renameLightRig(secondId, "Second", &error));
    REQUIRE(light_rig::findLightRig(project, secondId)->name == "Second");
  }

  WHEN("renaming to an invalid format")
  {
    REQUIRE_FALSE(projectContext.renameLightRig(defaultId, "bad/name", &error));
    REQUIRE_FALSE(error.empty());
  }
}

SCENARIO("SciVis Studio programmatic rig names are sanitized and unique",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();

  auto *a = projectContext.createLightRig("weird/name");
  REQUIRE(a != nullptr);
  REQUIRE(validateRigName(a->name));

  auto *b = projectContext.createLightRig("weird/name");
  REQUIRE(b != nullptr);
  REQUIRE(validateRigName(b->name));
  REQUIRE(a->name != b->name);
}

SCENARIO("SciVis Studio v4 projects round-trip standalone Rig Archives",
    "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_v4_rigs";
  std::filesystem::remove_all(root);

  std::string defaultLightId;
  std::string cameraRigId;

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto &project = projectContext.project();

    // A second light rig with two lights, plus keyframe data on a camera rig.
    auto *second = projectContext.createLightRig("Rim");
    REQUIRE(second != nullptr);
    projectContext.addLightToRig(*second, "directional");
    projectContext.addLightToRig(*second, "point");

    defaultLightId = project.lightRigs.front().id;

    auto *cameraRig = &project.cameraRigs.front();
    cameraRigId = cameraRig->id;
    CameraKeyframe kf;
    kf.frame = 7;
    kf.manipulator.orbit.lookat = {4.f, 5.f, 6.f};
    cameraRig->keyframes.push_back(kf);

    REQUIRE(projectContext.saveProject(root));

    THEN("one Archive is written per rig")
    {
      REQUIRE(std::filesystem::exists(root / "cameras"));
      REQUIRE(std::filesystem::exists(root / "lights"));
      size_t lightFiles = 0;
      for (auto &e : std::filesystem::directory_iterator(root / "lights"))
        if (e.path().extension() == ".vsr")
          ++lightFiles;
      REQUIRE(lightFiles == project.lightRigs.size());
    }
  }

  WHEN("the project is reopened")
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();

    THEN("light rigs and their lights are restored from Archives")
    {
      REQUIRE(project.lightRigs.size() == 2);
      LightRig *rim = nullptr;
      for (auto &r : project.lightRigs) {
        if (r.name == "Rim")
          rim = &r;
      }
      REQUIRE(rim != nullptr);
      auto rimRoot = projectContext.resolveLightRigRoot(*rim);
      REQUIRE(rimRoot);
      int lightCount = 0;
      auto *layer = (*rimRoot)->layer();
      layer->traverse(rimRoot, [&](auto &node, int) {
        if (node->isObject() && node->type() == ANARI_LIGHT)
          ++lightCount;
        return true;
      });
      REQUIRE(lightCount == 2);
    }

    THEN("camera rig keyframes are restored from Archives")
    {
      auto *cameraRig = camera_rig::findCameraRig(project, cameraRigId);
      REQUIRE(cameraRig != nullptr);
      REQUIRE(cameraRig->keyframes.size() == 1);
      REQUIRE(cameraRig->keyframes.front().frame == 7);
    }
  }

  WHEN("a rig is renamed and the project is saved again")
  {
    std::string oldLightName;
    const auto unlisted = root / "lights/Unlisted.vsr";
    {
      vsr::app::Context appContext;
      ProjectContext projectContext(&appContext);
      REQUIRE(projectContext.openProject(root));
      auto &project = projectContext.project();
      auto *defaultRig = light_rig::findLightRig(project, defaultLightId);
      REQUIRE(defaultRig != nullptr);
      oldLightName = defaultRig->name;
      std::filesystem::copy_file(
          root / "lights" / (oldLightName + ".vsr"), unlisted);
      REQUIRE(projectContext.renameLightRig(defaultLightId, "Key Light"));
      REQUIRE(projectContext.saveProject(root));
    }

    THEN("only the explicitly superseded Rig Archive is removed")
    {
      REQUIRE(std::filesystem::exists(root / "lights" / "Key Light.vsr"));
      REQUIRE_FALSE(
          std::filesystem::exists(root / "lights" / (oldLightName + ".vsr")));
      REQUIRE(std::filesystem::exists(unlisted));
    }
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio tolerates a missing Light Rig Archive on open",
    "[SciVisStudio]")
{
  const auto root =
      std::filesystem::temp_directory_path() / "vsr_scivis_studio_missing_rig";
  std::filesystem::remove_all(root);

  std::string rimId;
  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    projectContext.createUnsavedProject();
    auto *rim = projectContext.createLightRig("Rim");
    REQUIRE(rim != nullptr);
    projectContext.addLightToRig(*rim, "directional");
    rimId = rim->id;
    REQUIRE(projectContext.saveProject(root));
  }

  // Corrupt the project by deleting one rig's file.
  std::filesystem::remove(root / "lights" / "Rim.vsr");

  {
    vsr::app::Context appContext;
    ProjectContext projectContext(&appContext);
    REQUIRE(projectContext.openProject(root));
    auto &project = projectContext.project();

    THEN("the missing rig is skipped and the rest of the project opens")
    {
      REQUIRE(light_rig::findLightRig(project, rimId) == nullptr);
      REQUIRE_FALSE(project.lightRigs.empty()); // default rig survived
    }
  }

  std::filesystem::remove_all(root);
}

SCENARIO("SciVis Studio generated ids skip ids still in use", "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto &project = projectContext.project();

  GIVEN("a project whose second of three shots was removed")
  {
    REQUIRE(projectContext.addShot("two"));
    REQUIRE(projectContext.addShot("three"));
    const auto second = project.shots[1].id;
    const auto third = project.shots[2].id;
    REQUIRE(projectContext.removeShot(second));

    THEN("the next shot id is not the surviving third shot's")
    {
      const auto next = project::nextShotId(project);
      REQUIRE(next != third);
      REQUIRE(project::findShot(project, next) == nullptr);
      REQUIRE(projectContext.addShot("four"));
      REQUIRE(project.shots.back().id == next);
    }
  }

  GIVEN("rig and color map libraries with a gap")
  {
    auto *lightA = projectContext.createLightRig("A");
    REQUIRE(lightA);
    const auto lightAId = lightA->id;
    REQUIRE(projectContext.createLightRig("B"));
    REQUIRE(projectContext.removeLightRig(lightAId));

    REQUIRE(projectContext.createCameraRig("A"));
    REQUIRE(projectContext.createCameraRig("B"));
    REQUIRE(projectContext.removeCameraRig(project.cameraRigs[1].id));

    REQUIRE(projectContext.createColorMap("A"));
    REQUIRE(projectContext.createColorMap("B"));
    REQUIRE(projectContext.removeColorMap(project.colorMaps.front().id));

    THEN("every generator mints an unused id")
    {
      REQUIRE(
          light_rig::findLightRig(project, light_rig::nextLightRigId(project))
          == nullptr);
      REQUIRE(camera_rig::findCameraRig(
                  project, camera_rig::nextCameraRigId(project))
          == nullptr);
      REQUIRE(project::findColorMap(project, project::nextColorMapId(project))
          == nullptr);
    }
  }
}

SCENARIO(
    "SciVis Studio shots are removed, updated and activated as whole "
    "operations",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto &project = projectContext.project();
  auto &scene = appContext.vsr.scene;
  std::string error;

  GIVEN("a single-shot project")
  {
    THEN("the last shot cannot be removed")
    {
      REQUIRE_FALSE(
          projectContext.removeShot(project.shots.front().id, &error));
      REQUIRE(error.find("last shot") != std::string::npos);
      REQUIRE(project.shots.size() == 1);
    }

    THEN("unknown ids are rejected by every shot call")
    {
      REQUIRE_FALSE(projectContext.removeShot("nope", &error));
      REQUIRE(error == "shot not found");
      REQUIRE_FALSE(projectContext.setActiveShot("nope", &error));
      REQUIRE(error == "shot not found");
      Shot stranger;
      stranger.id = "nope";
      REQUIRE_FALSE(projectContext.updateShot(stranger, &error));
      REQUIRE(error == "shot not found");
    }
  }

  GIVEN("two shots with the second active")
  {
    const auto firstId = project.shots.front().id;
    REQUIRE(projectContext.addShot("second"));
    const auto secondId = project.shots.back().id;
    REQUIRE(project.activeShotId == secondId);
    const auto cameraCount = scene.numberOfObjects(ANARI_CAMERA);

    WHEN("the active shot is removed")
    {
      REQUIRE(projectContext.removeShot(secondId, &error));

      THEN("the first shot becomes active and the camera object is gone")
      {
        REQUIRE(project.shots.size() == 1);
        REQUIRE(project.activeShotId == firstId);
        REQUIRE(scene.numberOfObjects(ANARI_CAMERA) == cameraCount - 1);
        auto *layer = scene.layer("studio");
        auto shotsRoot = findDirectChild(layer->root(), "shots");
        REQUIRE_FALSE(findDirectChild(shotsRoot, secondId));
        REQUIRE(findDirectChild(shotsRoot, firstId));
        REQUIRE(project.dirty);
      }
    }

    WHEN("the first shot is activated")
    {
      project.markClean();
      REQUIRE(projectContext.setActiveShot(firstId, &error));

      THEN("it is active, the project is dirty and the animation follows")
      {
        REQUIRE(project.activeShotId == firstId);
        REQUIRE(project.dirty);
        REQUIRE(appContext.vsr.animationMgr.getAnimationFrame()
            == project.shots.front().currentFrame);
      }
    }

    WHEN("the active shot is updated with out-of-range fields")
    {
      Shot edit = *project::findShot(project, secondId);
      edit.name = "renamed";
      edit.frameCount = 0;
      edit.fps = 0.f;
      edit.currentFrame = 50;
      edit.playing = true;
      edit.camera = {ANARI_CAMERA, 12345};
      edit.renderSettings.width = 0;
      edit.datasetBindings.push_back({"dataset_9999", true});
      REQUIRE(projectContext.updateShot(edit, &error));

      THEN("the stored shot is the normalized copy")
      {
        const auto *shot = project::findShot(project, secondId);
        REQUIRE(shot->name == "renamed");
        REQUIRE(shot->frameCount == 1);
        REQUIRE(shot->fps == 1.f);
        REQUIRE(shot->currentFrame == 0);
        REQUIRE_FALSE(shot->playing);
        REQUIRE(shot->camera.objectIndex != 12345);
        REQUIRE(shot->renderSettings.width == 1);
        REQUIRE(shot->datasetBindings.empty());
      }
    }

    WHEN("the active shot is updated while it plays")
    {
      REQUIRE(projectContext.setActiveShot(secondId, &error));
      REQUIRE(projectContext.setPlaying(secondId, true, &error));
      projectContext.setActiveShotFrame(7);
      REQUIRE(project::findShot(project, secondId)->currentFrame == 7);

      Shot edit = *project::findShot(project, secondId);
      edit.loop = false;
      edit.currentFrame = 2; // the frame an editor last saw
      REQUIRE(projectContext.updateShot(edit, &error));

      THEN("the edit lands but the frame in motion is kept")
      {
        const auto *shot = project::findShot(project, secondId);
        REQUIRE_FALSE(shot->loop);
        REQUIRE(shot->playing);
        REQUIRE(shot->currentFrame == 7);
        REQUIRE(appContext.vsr.animationMgr.getAnimationFrame() == 7);
        REQUIRE(appContext.vsr.animationMgr.isPlaying());
      }
    }

    WHEN("an update names an unknown rig or a foreign renderer")
    {
      Shot edit = *project::findShot(project, secondId);
      edit.lightRigId = "lightRig_9999";
      REQUIRE_FALSE(projectContext.updateShot(edit, &error));
      REQUIRE(error == "light rig not found");

      edit = *project::findShot(project, secondId);
      edit.cameraRigId = "cameraRig_9999";
      REQUIRE_FALSE(projectContext.updateShot(edit, &error));
      REQUIRE(error == "camera rig not found");

      edit = *project::findShot(project, secondId);
      edit.renderSettings.rendererObjectIndex = 7;
      REQUIRE_FALSE(projectContext.updateShot(edit, &error));
      REQUIRE(error.find("renderer") != std::string::npos);

      THEN("the stored shot is untouched")
      {
        const auto *shot = project::findShot(project, secondId);
        REQUIRE(shot->lightRigId == project.lightRigs.front().id);
        REQUIRE(shot->renderSettings.rendererObjectIndex == VSR_INVALID_INDEX);
      }
    }
  }
}

SCENARIO("SciVis Studio shot ops validate and replace the record on their own",
    "[SciVisStudio]")
{
  Project project;
  Shot first;
  first.id = "shot_0001";
  first.name = "first";
  Shot second;
  second.id = "shot_0002";
  second.name = "second";
  project.shots = {first, second};
  project.activeShotId = second.id;
  std::string error;
  bool activeChanged = true;

  GIVEN("an edit of the active shot with out-of-range fields, and no scene")
  {
    Shot edit = second;
    edit.frameCount = 0;
    edit.currentFrame = 9;
    edit.playing = true;
    edit.datasetBindings.push_back({"dataset_9999", true});
    edit.renderSettings.rendererObjectIndex = 7; // unchecked without a scene

    WHEN("it is applied")
    {
      REQUIRE(shot::updateShot(project, nullptr, edit, &error));

      THEN("the record is the normalized copy and nothing is marked dirty")
      {
        const auto *shot = project::findShot(project, second.id);
        REQUIRE(shot->frameCount == 1);
        REQUIRE(shot->currentFrame == 0);
        REQUIRE_FALSE(shot->playing);
        REQUIRE(shot->datasetBindings.empty());
        REQUIRE(shot->renderSettings.rendererObjectIndex == 7);
        REQUIRE_FALSE(project.dirty);
      }
    }

    WHEN("it names a rig the project does not have")
    {
      edit.lightRigId = "lightRig_9999";
      REQUIRE_FALSE(shot::updateShot(project, nullptr, edit, &error));

      THEN("it is rejected and the record is untouched")
      {
        REQUIRE(error == "light rig not found");
        REQUIRE(project::findShot(project, second.id)->frameCount
            == second.frameCount);
      }
    }
  }

  GIVEN("two shots, the second active")
  {
    WHEN("the inactive one is removed")
    {
      REQUIRE(
          shot::removeShot(project, nullptr, first.id, activeChanged, &error));

      THEN("the active shot stands and the last one cannot go")
      {
        REQUIRE_FALSE(activeChanged);
        REQUIRE(project.activeShotId == second.id);
        REQUIRE(project.shots.size() == 1);
        REQUIRE_FALSE(shot::removeShot(
            project, nullptr, second.id, activeChanged, &error));
        REQUIRE(error == "cannot remove the last shot");
        REQUIRE_FALSE(project.dirty);
      }
    }

    WHEN("the active one is removed")
    {
      REQUIRE(
          shot::removeShot(project, nullptr, second.id, activeChanged, &error));

      THEN("the first remaining shot is reported as the new active one")
      {
        REQUIRE(activeChanged);
        REQUIRE(project.activeShotId == first.id);
      }
    }

    WHEN("an unknown id is removed")
    {
      REQUIRE_FALSE(
          shot::removeShot(project, nullptr, "nope", activeChanged, &error));

      THEN("it is reported and nothing changed")
      {
        REQUIRE(error == "shot not found");
        REQUIRE_FALSE(activeChanged);
        REQUIRE(project.shots.size() == 2);
      }
    }
  }
}

SCENARIO("SciVis Studio color map arrays are the model's to pair and find",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  auto &scene = appContext.vsr.scene;
  Project project;
  std::string error;

  GIVEN("records as a manifest loads them, with no arrays yet")
  {
    project.colorMaps.push_back({"cm_0001", "Heat"});
    project.colorMaps.push_back({"cm_0002", "Cold"});
    REQUIRE_FALSE(color_map::resolveColorMapArray(scene, "cm_0001"));

    WHEN("the arrays are ensured twice")
    {
      const auto before = scene.numberOfObjects(ANARI_ARRAY1D);
      color_map::ensureColorMapArrays(project, scene);
      color_map::ensureColorMapArrays(project, scene);

      THEN("each record has exactly one default array, named by its id")
      {
        REQUIRE(scene.numberOfObjects(ANARI_ARRAY1D) == before + 2);
        auto heat = color_map::resolveColorMapArray(scene, "cm_0001");
        REQUIRE(heat);
        REQUIRE(heat->name() == "cm_0001_colormap");
        REQUIRE(heat->elementType() == ANARI_FLOAT32_VEC4);
        REQUIRE(heat->size() == 256);
        REQUIRE(color_map::resolveColorMapArray(scene, "cm_0002"));
        REQUIRE_FALSE(color_map::resolveColorMapArray(scene, "cm_0003"));
      }
    }
  }

  GIVEN("a record created together with its array")
  {
    auto &record = color_map::createColorMap(project, scene, "Heat");
    REQUIRE(record.name == "Heat");
    const auto id = record.id;
    REQUIRE(color_map::resolveColorMapArray(scene, id));

    THEN("the dirty flag is the caller's, not the pairing's")
    {
      REQUIRE_FALSE(project.dirty);
    }

    THEN("removing it takes the array; an unknown id is reported")
    {
      REQUIRE(color_map::removeColorMap(project, scene, id, &error));
      REQUIRE(project.colorMaps.empty());
      REQUIRE_FALSE(color_map::resolveColorMapArray(scene, id));
      REQUIRE_FALSE(color_map::removeColorMap(project, scene, id, &error));
      REQUIRE(error == "color map not found");
    }
  }
}

SCENARIO("SciVis Studio color maps pair a record with a scene array",
    "[SciVisStudio]")
{
  vsr::app::Context appContext;
  ProjectContext projectContext(&appContext);
  projectContext.createUnsavedProject();
  auto &project = projectContext.project();
  auto &scene = appContext.vsr.scene;
  std::string error;

  WHEN("a color map is created")
  {
    const auto arrays = scene.numberOfObjects(ANARI_ARRAY1D);
    auto *record = projectContext.createColorMap("Heat");
    REQUIRE(record);
    const auto id = record->id;

    THEN("the record names an RGBA array that lives in the scene")
    {
      REQUIRE(record->name == "Heat");
      REQUIRE(scene.numberOfObjects(ANARI_ARRAY1D) == arrays + 1);
      auto array = projectContext.resolveColorMapArray(id);
      REQUIRE(array);
      REQUIRE(array->name() == id + "_colormap");
      REQUIRE(array->elementType() == ANARI_FLOAT32_VEC4);
      REQUIRE(array->size() == 256);
      REQUIRE(project.dirty);
    }

    THEN("a second one with the same name is de-duplicated")
    {
      auto *other = projectContext.createColorMap("heat");
      REQUIRE(other);
      REQUIRE(other->name != "Heat");
      REQUIRE(other->id != id);
    }

    THEN("rename validates format and collisions")
    {
      REQUIRE(projectContext.createColorMap("Cold"));
      REQUIRE_FALSE(projectContext.renameColorMap(id, "cold", &error));
      REQUIRE(error.find("already uses") != std::string::npos);
      REQUIRE_FALSE(projectContext.renameColorMap(id, "", &error));
      REQUIRE_FALSE(projectContext.renameColorMap("nope", "x", &error));
      REQUIRE(error == "color map not found");
      REQUIRE(projectContext.renameColorMap(id, "Warm", &error));
      REQUIRE(project::findColorMap(project, id)->name == "Warm");
      REQUIRE(projectContext.resolveColorMapArray(id));
    }

    THEN("remove takes the record and the array with it")
    {
      REQUIRE(projectContext.removeColorMap(id, &error));
      REQUIRE(project.colorMaps.empty());
      REQUIRE_FALSE(projectContext.resolveColorMapArray(id));
      REQUIRE(scene.numberOfObjects(ANARI_ARRAY1D) == arrays);
      REQUIRE_FALSE(projectContext.removeColorMap(id, &error));
      REQUIRE(error == "color map not found");
    }
  }
}
