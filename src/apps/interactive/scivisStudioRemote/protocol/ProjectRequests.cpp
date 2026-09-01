// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectRequests.h"
// vsr_scivis_studio_model
#include "ProjectContext.h"

namespace vsr::scivis_studio::protocol {

namespace {

void writeImporterType(vsr::core::DataNode &n, vsr::io::ImporterType type)
{
  writeChild(n, "importerType", std::string(toString(type)));
}

bool readImporterType(const vsr::core::DataNode &n, vsr::io::ImporterType &out)
{
  return readEnumChild(n, "importerType", out, importerTypeFromString);
}

void writePathList(vsr::core::DataNode &parent,
    const char *name,
    const std::vector<std::filesystem::path> &paths)
{
  std::vector<std::string> items;
  items.reserve(paths.size());
  for (const auto &p : paths)
    items.push_back(p.generic_string());
  writeStringList(parent, name, items);
}

bool readPathList(const vsr::core::DataNode &parent,
    const char *name,
    std::vector<std::filesystem::path> &out)
{
  std::vector<std::string> items;
  if (!readStringList(parent, name, items))
    return false;
  out.clear();
  out.reserve(items.size());
  for (const auto &s : items)
    out.emplace_back(s);
  return true;
}

} // namespace

// Importer type names ////////////////////////////////////////////////////////

const char *toString(vsr::io::ImporterType importerType)
{
  return vsr::scivis_studio::toString(importerType);
}

std::optional<vsr::io::ImporterType> importerTypeFromString(
    const std::string &name)
{
  // The model's parser answers NONE for unknown names; only accept NONE when
  // the wire actually said so.
  const auto type = vsr::scivis_studio::importerTypeFromString(name);
  if (name != vsr::scivis_studio::toString(type))
    return {};
  return type;
}

// Project ////////////////////////////////////////////////////////////////////

void toNode(const NewProject &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
}

bool fromNode(const vsr::core::DataNode &n, NewProject &r)
{
  return readChild(n, "requestId", r.requestId);
}

void toNode(const OpenProject &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writePath(n, "directory", r.directory);
}

bool fromNode(const vsr::core::DataNode &n, OpenProject &r)
{
  return readChild(n, "requestId", r.requestId)
      && readPath(n, "directory", r.directory);
}

void toNode(const SaveProject &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  if (r.directory)
    writePath(n, "directory", *r.directory);
  writeSubtree(n, "uiState", r.uiState);
}

bool fromNode(const vsr::core::DataNode &n, SaveProject &r)
{
  if (!readChild(n, "requestId", r.requestId))
    return false;
  r.directory.reset();
  if (hasChild(n, "directory")) {
    std::filesystem::path directory;
    if (!readPath(n, "directory", directory))
      return false;
    r.directory = std::move(directory);
  }
  r.uiState = readSubtree(n, "uiState");
  return true;
}

// Dataset creation ///////////////////////////////////////////////////////////

void toNode(const ImportStaticDataset &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChild(n, "name", r.name);
  writePath(n, "sourcePath", r.sourcePath);
  writeImporterType(n, r.importerType);
  writeChild(n, "fromSubtreeArchive", r.fromSubtreeArchive);
}

bool fromNode(const vsr::core::DataNode &n, ImportStaticDataset &r)
{
  if (!readChild(n, "requestId", r.requestId) || !readChild(n, "name", r.name)
      || !readPath(n, "sourcePath", r.sourcePath)
      || !readImporterType(n, r.importerType))
    return false;
  r.fromSubtreeArchive = readChildOr(n, "fromSubtreeArchive", false);
  return true;
}

void toNode(const ImportFileAnimationDataset &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChild(n, "name", r.name);
  writePathList(n, "sourcePaths", r.sourcePaths);
  writeImporterType(n, r.importerType);
  writeChild(n, "setActiveShotFrameCount", r.setActiveShotFrameCount);
}

bool fromNode(const vsr::core::DataNode &n, ImportFileAnimationDataset &r)
{
  if (!readChild(n, "requestId", r.requestId) || !readChild(n, "name", r.name)
      || !readPathList(n, "sourcePaths", r.sourcePaths)
      || !readImporterType(n, r.importerType))
    return false;
  r.setActiveShotFrameCount = readChildOr(n, "setActiveShotFrameCount", true);
  return true;
}

void toNode(const DeclareFileAnimationDataset &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChild(n, "name", r.name);
  writeStringList(n, "sourceList", r.sourceList);
  writeImporterType(n, r.importerType);
  writeChild(n, "setActiveShotFrameCount", r.setActiveShotFrameCount);
}

bool fromNode(const vsr::core::DataNode &n, DeclareFileAnimationDataset &r)
{
  if (!readChild(n, "requestId", r.requestId) || !readChild(n, "name", r.name)
      || !readStringList(n, "sourceList", r.sourceList)
      || !readImporterType(n, r.importerType))
    return false;
  r.setActiveShotFrameCount = readChildOr(n, "setActiveShotFrameCount", true);
  return true;
}

// Dataset edits and residency ////////////////////////////////////////////////

// Requests whose only field beyond requestId is the target dataset id.
#define VSR_STUDIO_DATASET_ID_PAYLOAD(T)                                       \
  void toNode(const T &r, vsr::core::DataNode &n)                              \
  {                                                                            \
    writeChild(n, "requestId", r.requestId);                                   \
    writeChild(n, "datasetId", r.datasetId);                                   \
  }                                                                            \
  bool fromNode(const vsr::core::DataNode &n, T &r)                            \
  {                                                                            \
    return readChild(n, "requestId", r.requestId)                              \
        && readChild(n, "datasetId", r.datasetId);                             \
  }

VSR_STUDIO_DATASET_ID_PAYLOAD(ReimportDataset)
VSR_STUDIO_DATASET_ID_PAYLOAD(LoadDataset)
VSR_STUDIO_DATASET_ID_PAYLOAD(UnloadDataset)
VSR_STUDIO_DATASET_ID_PAYLOAD(RefreshDatasetAvailability)

#undef VSR_STUDIO_DATASET_ID_PAYLOAD

void toNode(const RenameDataset &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChild(n, "datasetId", r.datasetId);
  writeChild(n, "newName", r.newName);
}

bool fromNode(const vsr::core::DataNode &n, RenameDataset &r)
{
  return readChild(n, "requestId", r.requestId)
      && readChild(n, "datasetId", r.datasetId)
      && readChild(n, "newName", r.newName);
}

void toNode(const RemoveDataset &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChild(n, "datasetId", r.datasetId);
  writeChild(n, "keepAssetFile", r.keepAssetFile);
}

bool fromNode(const vsr::core::DataNode &n, RemoveDataset &r)
{
  if (!readChild(n, "requestId", r.requestId)
      || !readChild(n, "datasetId", r.datasetId))
    return false;
  r.keepAssetFile = readChildOr(n, "keepAssetFile", false);
  return true;
}

// Dataset archives and candidates ////////////////////////////////////////////

void toNode(const SaveDatasetArchive &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChild(n, "datasetId", r.datasetId);
  writePath(n, "file", r.file);
}

bool fromNode(const vsr::core::DataNode &n, SaveDatasetArchive &r)
{
  return readChild(n, "requestId", r.requestId)
      && readChild(n, "datasetId", r.datasetId) && readPath(n, "file", r.file);
}

void toNode(const LoadDatasetArchive &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writePath(n, "file", r.file);
}

bool fromNode(const vsr::core::DataNode &n, LoadDatasetArchive &r)
{
  return readChild(n, "requestId", r.requestId) && readPath(n, "file", r.file);
}

void toNode(const DiscoverDatasetCandidates &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
}

bool fromNode(const vsr::core::DataNode &n, DiscoverDatasetCandidates &r)
{
  return readChild(n, "requestId", r.requestId);
}

void toNode(const IncorporateDatasetCandidate &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writePath(n, "file", r.file);
  writeChild(n, "proposedName", r.proposedName);
  writeChild(n, "name", r.name);
}

bool fromNode(const vsr::core::DataNode &n, IncorporateDatasetCandidate &r)
{
  if (!readChild(n, "requestId", r.requestId) || !readPath(n, "file", r.file)
      || !readChild(n, "name", r.name))
    return false;
  r.proposedName = readChildOr(n, "proposedName", std::string());
  return true;
}

// Results ////////////////////////////////////////////////////////////////////

void toNode(const DatasetCreatedResult &r, vsr::core::DataNode &n)
{
  writeChild(n, "datasetId", r.datasetId);
}

bool fromNode(const vsr::core::DataNode &n, DatasetCreatedResult &r)
{
  return readChild(n, "datasetId", r.datasetId);
}

void toNode(const DatasetCandidateEntry &e, vsr::core::DataNode &n)
{
  writePath(n, "file", e.file);
  writeChild(n, "proposedName", e.proposedName);
}

bool fromNode(const vsr::core::DataNode &n, DatasetCandidateEntry &e)
{
  if (!readPath(n, "file", e.file))
    return false;
  e.proposedName = readChildOr(n, "proposedName", std::string());
  return true;
}

void toNode(const DiscoverDatasetCandidatesResult &r, vsr::core::DataNode &n)
{
  if (r.candidates.empty())
    return;
  auto &list = n["candidates"];
  for (size_t i = 0; i < r.candidates.size(); ++i)
    writeChildNode(list, std::to_string(i).c_str(), r.candidates[i]);
}

bool fromNode(const vsr::core::DataNode &n, DiscoverDatasetCandidatesResult &r)
{
  r.candidates.clear();
  const auto *list = n.child("candidates");
  if (!list)
    return true;
  bool ok = true;
  list->foreach_child_const([&](const vsr::core::DataNode &item) {
    DatasetCandidateEntry entry;
    if (!fromNode(item, entry)) {
      ok = false;
      return;
    }
    r.candidates.push_back(std::move(entry));
  });
  return ok;
}

} // namespace vsr::scivis_studio::protocol
