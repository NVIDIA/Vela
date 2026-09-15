// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectSerialization.h"
#include "DataNodeFields.h"

#include "vsr/core/DataTreeMetadata.hpp"
#include "vsr/io/archives/CameraArchive.hpp"
#include "vsr/io/archives/RendererArchive.hpp"

#include <anari/anari_cpp/ext/std.h>

#include <cctype>
#include <exception>
#include <optional>

namespace vsr::scivis_studio {

namespace {

constexpr size_t MAX_RIG_NAME_LENGTH = 128;

bool isAllowedRigNameChar(unsigned char c)
{
  return std::isalnum(c) || c == ' ' || c == '_' || c == '-' || c == '('
      || c == ')';
}

using PoolArchiveValidator = vsr::io::ArchiveValidationResult (*)(
    vsr::core::DataNode &);

bool validateRequiredPoolArchive(const std::filesystem::path &directory,
    const std::filesystem::path &relativePath,
    std::string_view expectedSchema,
    PoolArchiveValidator validate,
    std::string &error)
{
  const auto file = resolveProjectFileForRead(directory / relativePath);
  vsr::core::DataTree archive;
  try {
    if (!archive.load(file.string().c_str())) {
      error = "required " + relativePath.generic_string()
          + " is missing or unreadable";
      return false;
    }
  } catch (const std::exception &e) {
    error = "required " + relativePath.generic_string()
        + " is unreadable: " + e.what();
    return false;
  } catch (...) {
    error = "required " + relativePath.generic_string() + " is unreadable";
    return false;
  }

  const auto metadata = vsr::core::readDataTreeMetadata(archive.root());
  const auto validation = validate(archive.root());
  if (!metadata.found() || metadata.metadata->schema != expectedSchema
      || !validation.accepted()) {
    error = "required " + relativePath.generic_string() + " is invalid";
    if (!validation.message.empty())
      error += ": " + validation.message;
    return false;
  }
  return true;
}

} // namespace

bool validateRigName(const std::string &name, std::string *error)
{
  auto fail = [&](const char *msg) {
    if (error)
      *error = msg;
    return false;
  };

  if (name.empty())
    return fail("name cannot be empty");
  if (name.size() > MAX_RIG_NAME_LENGTH)
    return fail("name is too long");
  if (name == "." || name == "..")
    return fail("name is reserved");
  if (std::isspace(static_cast<unsigned char>(name.front()))
      || std::isspace(static_cast<unsigned char>(name.back())))
    return fail("name cannot start or end with whitespace");
  for (unsigned char c : name) {
    if (!isAllowedRigNameChar(c))
      return fail(
          "name may only contain letters, digits, spaces, '_', '-', '(', ')'");
  }

  if (error)
    error->clear();
  return true;
}

std::string sanitizeRigName(const std::string &name)
{
  std::string out;
  out.reserve(name.size());
  for (unsigned char c : name)
    out.push_back(isAllowedRigNameChar(c) ? static_cast<char>(c) : '_');

  // Trim leading/trailing whitespace.
  const auto first = out.find_first_not_of(' ');
  const auto last = out.find_last_not_of(' ');
  if (first == std::string::npos)
    out.clear();
  else
    out = out.substr(first, last - first + 1);

  if (out.size() > MAX_RIG_NAME_LENGTH)
    out.resize(MAX_RIG_NAME_LENGTH);

  if (out.empty() || out == "." || out == "..")
    out = "rig";

  return out;
}

namespace {

using vsr::core::DataNode;

std::string cameraRigNameForShot(const Shot &shot)
{
  if (!shot.name.empty())
    return shot.name + " Camera";
  if (!shot.id.empty())
    return shot.id + " Camera";
  return "Camera Rig";
}

// Strict parsers for the fields written from toString(). The lenient
// dataset::*FromString() stay on the v1-v4 path, whose files spell the legacy
// aliases ("TimeSeries", "Missing").
std::optional<DatasetStatus> statusFromName(const std::string &s)
{
  return enumFromName(s,
      DatasetStatus::Available,
      DatasetStatus::ImportFailed,
      dataset::toString);
}

std::optional<DatasetSourceKind> sourceKindFromName(const std::string &s)
{
  return enumFromName(
      s, DatasetSourceKind::Static, DatasetSourceKind::Live, dataset::toString);
}

std::optional<DatasetResidency> residencyFromName(const std::string &s)
{
  return enumFromName(s,
      DatasetResidency::Loaded,
      DatasetResidency::Unloaded,
      dataset::toString);
}

// Datasets ///////////////////////////////////////////////////////////////////

void sourceFileToNode(const DatasetSourceFile &f, DataNode &n)
{
  writeChild(n, "path", f.path);
  writeChild(n, "resolvedPath", f.resolvedPath);
}

bool nodeToSourceFile(const DataNode &n, DatasetSourceFile &f)
{
  return readChild(n, "path", f.path)
      && readOptionalChild(n, "resolvedPath", f.resolvedPath);
}

// v1-v4 manifests embedded the dataset payload metadata inline, spelling each
// path as absolutePath with a projectRelativePath fallback.
bool readLegacyPath(const DataNode &n, std::string &path)
{
  if (!readOptionalChild(n, "absolutePath", path))
    return false;
  return !path.empty() || readOptionalChild(n, "projectRelativePath", path);
}

bool nodeToLegacySourceFile(const DataNode &n, DatasetSourceFile &f)
{
  return readLegacyPath(n, f.path);
}

bool nodeToLegacyDatasetMetadata(const DataNode &n, Dataset &d)
{
  std::string text;
  if (!readChild(n, "sourceKind", text))
    return false;
  d.sourceKind = dataset::sourceKindFromString(text);
  if (!readOptionalChild(n, "importerType", d.importerType))
    return false;
  text = "Missing";
  if (!readOptionalChild(n, "status", text))
    return false;
  d.status = dataset::statusFromString(text);

  if (const auto *source = n.child("source")) {
    if (!readLegacyPath(*source, d.source.sourcePath))
      return false;
  }
  if (!readNodeList(n, "sourceFiles", d.sourceFiles, nodeToLegacySourceFile))
    return false;

  // Preserve the authoritative fields in memory and mark the dataset for
  // extraction into its own asset on the next explicit save.
  d.pendingExtraction = true;
  d.dirty = true;
  d.persistedName.clear();
  return true;
}

void datasetToNode(const Dataset &d, DataNode &n, ProjectForm form)
{
  writeChild(n, "id", d.id);
  writeChild(n, "name", d.name);
  writeChild(n, "residency", std::string(dataset::toString(d.residency)));
  if (form == ProjectForm::Manifest)
    return;

  writeChild(n, "status", std::string(dataset::toString(d.status)));
  writeChild(n, "sourceKind", std::string(dataset::toString(d.sourceKind)));
  writeChild(n, "importerType", d.importerType);
  auto &source = n["source"];
  writeChild(source, "sourcePath", d.source.sourcePath);
  if (d.source.importerSettings.size() > 0) {
    auto &settings = source["importerSettings"];
    for (const auto &kv : d.source.importerSettings)
      writeChild(settings, kv.first.c_str(), kv.second);
  }
  writeNodeList(n, "sourceFiles", d.sourceFiles, sourceFileToNode);
  writeChildNode(n, "rootNode", d.rootNode);
  writeChild(n, "dirty", d.dirty);
  writeChild(n, "declared", d.declared);
  writeChild(n, "pendingExtraction", d.pendingExtraction);
  writeChild(n, "pendingSourceListMigration", d.pendingSourceListMigration);
  writeChild(n, "persistedName", d.persistedName);
}

bool nodeToDatasetRuntime(const DataNode &n, Dataset &d)
{
  if (!readOptionalEnumChild(n, "status", d.status, statusFromName)
      || !readOptionalEnumChild(
          n, "sourceKind", d.sourceKind, sourceKindFromName)
      || !readOptionalChild(n, "importerType", d.importerType))
    return false;

  if (const auto *source = n.child("source")) {
    if (!readOptionalChild(*source, "sourcePath", d.source.sourcePath))
      return false;
    if (const auto *settings = source->child("importerSettings")) {
      bool ok = true;
      settings->foreach_child_const([&](const DataNode &kv) {
        if (!kv.getValue().is<std::string>()) {
          ok = false;
          return;
        }
        d.source.importerSettings.set(kv.name(), kv.getValueAs<std::string>());
      });
      if (!ok)
        return false;
    }
  }

  return readNodeList(n, "sourceFiles", d.sourceFiles, nodeToSourceFile)
      && readOptionalChildNode(n, "rootNode", d.rootNode)
      && readOptionalChild(n, "dirty", d.dirty)
      && readOptionalChild(n, "declared", d.declared)
      && readOptionalChild(n, "pendingExtraction", d.pendingExtraction)
      && readOptionalChild(
          n, "pendingSourceListMigration", d.pendingSourceListMigration)
      && readOptionalChild(n, "persistedName", d.persistedName);
}

bool nodeToDataset(const DataNode &n, Dataset &d, ProjectForm form)
{
  Dataset out;
  if (!readChild(n, "id", out.id))
    return false;
  out.name = out.id;
  if (!readOptionalChild(n, "name", out.name))
    return false;
  // Manifests that predate residency (schema < 7) mean Loaded.
  if (!readOptionalEnumChild(n, "residency", out.residency, residencyFromName))
    return false;
  // The manifest defaults: the open path rebuilds status and clears dirty
  // once the dataset asset is read.
  out.status = DatasetStatus::Unavailable;
  out.dirty = false;
  out.persistedName = out.name;

  if (form == ProjectForm::Manifest) {
    if (hasChild(n, "sourceKind") && !nodeToLegacyDatasetMetadata(n, out))
      return false;
  } else if (!nodeToDatasetRuntime(n, out)) {
    return false;
  }
  d = std::move(out);
  return true;
}

// Rigs and color maps ////////////////////////////////////////////////////////

void lightRigToNode(const LightRig &r, DataNode &n, ProjectForm form)
{
  writeChild(n, "id", r.id);
  writeChild(n, "name", r.name);
  if (form == ProjectForm::Manifest)
    return;
  writeChildNode(n, "rootNode", r.rootNode);
  writeChild(n, "persistedName", r.persistedName);
}

bool nodeToLightRig(const DataNode &n, LightRig &r, ProjectForm form)
{
  LightRig out;
  if (!readChild(n, "id", out.id) || !readOptionalChild(n, "name", out.name))
    return false;
  if (form == ProjectForm::Full
      && (!readOptionalChildNode(n, "rootNode", out.rootNode)
          || !readOptionalChild(n, "persistedName", out.persistedName)))
    return false;
  r = std::move(out);
  return true;
}

// The manifest records only id and name: camera-rig value data lives in
// cameras/<name>.vsr (mirroring light rigs). The Full form carries it under
// "rig", the same child the pre-v4 manifest embedded it in, so one read
// serves both.
void cameraRigToNode(const CameraRig &r, DataNode &n, ProjectForm form)
{
  writeChild(n, "id", r.id);
  writeChild(n, "name", r.name);
  if (form == ProjectForm::Manifest)
    return;
  camera_rig::cameraRigToNode(r, n["rig"]);
  writeChild(n, "persistedName", r.persistedName);
}

bool nodeToCameraRig(const DataNode &n, CameraRig &r, ProjectForm form)
{
  CameraRig out;
  if (!readChild(n, "id", out.id) || !readOptionalChild(n, "name", out.name))
    return false;
  if (const auto *rig = n.child("rig")) {
    if (!camera_rig::nodeToCameraRig(*rig, out))
      return false;
  }
  if (form == ProjectForm::Full
      && !readOptionalChild(n, "persistedName", out.persistedName))
    return false;
  r = std::move(out);
  return true;
}

void colorMapToNode(const ColorMapRecord &c, DataNode &n)
{
  writeChild(n, "id", c.id);
  writeChild(n, "name", c.name);
}

bool nodeToColorMap(const DataNode &n, ColorMapRecord &c)
{
  ColorMapRecord out;
  if (!readChild(n, "id", out.id) || !readOptionalChild(n, "name", out.name))
    return false;
  c = std::move(out);
  return true;
}

// Shots //////////////////////////////////////////////////////////////////////

// v2 manifests embedded a shot's camera rig under "cameraRig"; it becomes a
// project-level rig the shot refers to.
bool migrateInlineCameraRig(const DataNode &s, Shot &shot, Project &project)
{
  const auto *rigNode = s.child("cameraRig");
  if (!rigNode || !shot.cameraRigId.empty())
    return true;
  CameraRig rig;
  rig.id = camera_rig::nextCameraRigId(project);
  rig.name = cameraRigNameForShot(shot);
  if (!camera_rig::nodeToCameraRig(*rigNode, rig))
    return false;
  shot.cameraRigId = rig.id;
  project.cameraRigs.push_back(std::move(rig));
  return true;
}

} // namespace

void projectToNode(const Project &project, DataNode &node, ProjectForm form)
{
  node.reset();
  writeChild(node, "name", project.name);
  writeChild(node, "projectDirectory", project.projectDirectory.string());
  writeChild(node, "activeShot", project.activeShotId);
  writeChild(node, "nextDatasetOrdinal", project.nextDatasetOrdinal);
  writeChild(node, "dirty", project.dirty);

  writeAppendedList(
      node, "datasets", project.datasets, [&](const Dataset &d, DataNode &n) {
        datasetToNode(d, n, form);
      });
  writeAppendedList(
      node, "shots", project.shots, [&](const Shot &s, DataNode &n) {
        toNode(s, n, form);
      });
  writeAppendedList(node,
      "lightRigs",
      project.lightRigs,
      [&](const LightRig &r, DataNode &n) { lightRigToNode(r, n, form); });
  writeAppendedList(node,
      "cameraRigs",
      project.cameraRigs,
      [&](const CameraRig &r, DataNode &n) { cameraRigToNode(r, n, form); });
  writeAppendedList(node, "colorMaps", project.colorMaps, colorMapToNode);
}

bool nodeToProject(const DataNode &node, Project &project, ProjectForm form)
{
  Project out;
  std::string projectDirectory;
  if (!readOptionalChild(node, "name", out.name)
      || !readOptionalChild(node, "projectDirectory", projectDirectory)
      || !readOptionalChild(node, "activeShot", out.activeShotId)
      || !readOptionalChild(node, "nextDatasetOrdinal", out.nextDatasetOrdinal)
      || !readOptionalChild(node, "dirty", out.dirty))
    return false;
  out.projectDirectory = projectDirectory;

  if (!readNodeList(node,
          "datasets",
          out.datasets,
          [&](const DataNode &d, Dataset &dataset) {
            return nodeToDataset(d, dataset, form);
          }))
    return false;

  // Legacy manifests did not persist the allocator. Start beyond every
  // generated ID still present so a later removal cannot collide with one.
  for (const auto &dataset : out.datasets) {
    constexpr const char *prefix = "dataset_";
    if (dataset.id.rfind(prefix, 0) != 0)
      continue;
    try {
      out.nextDatasetOrdinal = std::max(out.nextDatasetOrdinal,
          static_cast<uint64_t>(std::stoull(dataset.id.substr(8)) + 1));
    } catch (...) {
    }
  }

  // Camera rigs before shots: a migrated inline rig takes the next free id.
  if (!readNodeList(node,
          "cameraRigs",
          out.cameraRigs,
          [&](const DataNode &r, CameraRig &rig) {
            return nodeToCameraRig(r, rig, form);
          }))
    return false;

  if (!readNodeList(
          node, "shots", out.shots, [&](const DataNode &s, Shot &shot) {
            return fromNode(s, shot)
                && (form == ProjectForm::Full
                    || migrateInlineCameraRig(s, shot, out));
          }))
    return false;

  if (!readNodeList(node,
          "lightRigs",
          out.lightRigs,
          [&](const DataNode &r, LightRig &rig) {
            return nodeToLightRig(r, rig, form);
          }))
    return false;

  if (!readNodeList(node, "colorMaps", out.colorMaps, nodeToColorMap))
    return false;

  if (out.activeShotId.empty() && !out.shots.empty())
    out.activeShotId = out.shots.front().id;

  project = std::move(out);
  return true;
}

std::filesystem::path resolveProjectFileForRead(std::filesystem::path file)
{
  std::error_code ec;
  if (std::filesystem::exists(file, ec) && !ec)
    return file;

  auto legacy = file;
  legacy.replace_extension(LEGACY_PROJECT_FILE_EXTENSION);
  ec.clear();
  if (std::filesystem::exists(legacy, ec) && !ec)
    return legacy;

  return file;
}

bool isProjectFileExtension(const std::filesystem::path &extension)
{
  return extension == PROJECT_FILE_EXTENSION
      || extension == LEGACY_PROJECT_FILE_EXTENSION;
}

ProjectValidationResult validateProjectRoot(
    const std::filesystem::path &directory)
{
  ProjectValidationResult result;
  result.manifestPath =
      resolveProjectFileForRead(directory / PROJECT_MANIFEST_FILENAME);

  if (!std::filesystem::exists(directory)) {
    result.error = "project directory does not exist";
    return result;
  }

  if (!std::filesystem::is_directory(directory)) {
    result.error = "selected path is not a directory";
    return result;
  }

  if (!std::filesystem::exists(result.manifestPath)) {
    result.error = "project.vsr does not exist";
    return result;
  }

  vsr::core::DataTree tree;
  if (!tree.load(result.manifestPath.string().c_str())) {
    result.error = "failed to load project.vsr";
    return result;
  }

  auto &root = tree.root();
  auto metadataResult = vsr::core::readDataTreeMetadata(root);
  if (metadataResult.malformed()) {
    result.error = "malformed __vsr_metadata: " + metadataResult.message;
    return result;
  }

  int schemaVersion = 0;
  if (metadataResult.found()) {
    const auto &metadata = *metadataResult.metadata;
    schemaVersion = metadata.schemaVersion;
    if (metadata.envelopeVersion
        != vsr::core::DATA_TREE_METADATA_ENVELOPE_VERSION) {
      result.error = "unsupported SciVis Studio metadata envelopeVersion";
      return result;
    }

    if (metadata.fileType != PROJECT_FILE_TYPE
        || metadata.schema != PROJECT_SCHEMA) {
      result.error = "metadata schema is not SciVis Studio project";
      return result;
    }

    if (metadata.schemaVersion < 1 || metadata.schemaVersion > SCHEMA_VERSION) {
      result.error = "unsupported SciVis Studio schemaVersion";
      return result;
    }
  } else {
    const auto kind = root["projectKind"].getValueOr<std::string>("");
    if (kind != PROJECT_KIND) {
      result.error = "missing __vsr_metadata";
      return result;
    }

    schemaVersion = root["schemaVersion"].getValueOr<int>(0);
    if (schemaVersion < 1 || schemaVersion > SCHEMA_VERSION) {
      result.error = "unsupported legacy SciVis Studio schemaVersion";
      return result;
    }
  }

  if (schemaVersion >= DECOMPOSED_SCENE_SCHEMA_VERSION
      && (!validateRequiredPoolArchive(directory,
              std::filesystem::path("scene") / "cameras.vsr",
              "vsr.scene.cameras",
              vsr::io::validate_CameraArchive,
              result.error)
          || !validateRequiredPoolArchive(directory,
              std::filesystem::path("scene") / "renderers.vsr",
              "vsr.scene.renderers",
              vsr::io::validate_RendererArchive,
              result.error))) {
    return result;
  }

  result.ok = true;
  return result;
}

} // namespace vsr::scivis_studio
