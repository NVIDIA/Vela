// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectRequests.h"
#include "PayloadMacros.h"
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

} // namespace

// Request plumbing ///////////////////////////////////////////////////////////

std::optional<uint64_t> peekRequestId(const vsr::network::Message &msg)
{
  vsr::core::DataTree tree;
  if (!msg.payload.empty() && !tree.read(msg.payload))
    return {};
  uint64_t requestId = 0;
  if (!readChild(tree.root(), "requestId", requestId))
    return {};
  return requestId;
}

// Importer type names ////////////////////////////////////////////////////////

const char *toString(vsr::io::ImporterType importerType)
{
  return vsr::scivis_studio::toString(importerType);
}

std::optional<vsr::io::ImporterType> importerTypeFromString(
    const std::string &name)
{
  return enumFromName(name,
      vsr::io::ImporterType::AGX,
      vsr::io::ImporterType::NONE,
      vsr::scivis_studio::toString);
}

// Project ////////////////////////////////////////////////////////////////////

VSR_STUDIO_BARE_REQUEST(NewProject)

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

VSR_STUDIO_ID_REQUEST(ReimportDataset, datasetId)
VSR_STUDIO_ID_REQUEST(LoadDataset, datasetId)
VSR_STUDIO_ID_REQUEST(UnloadDataset, datasetId)
VSR_STUDIO_ID_REQUEST(RefreshDatasetAvailability, datasetId)
VSR_STUDIO_RENAME_REQUEST(RenameDataset, datasetId)

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

VSR_STUDIO_ARCHIVE_SAVE_REQUEST(SaveDatasetArchive, datasetId)
VSR_STUDIO_ARCHIVE_LOAD_REQUEST(LoadDatasetArchive)
VSR_STUDIO_BARE_REQUEST(DiscoverDatasetCandidates)

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

VSR_STUDIO_ID_RESULT(DatasetCreatedResult, datasetId)

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
  writeNodeList(n, "candidates", r.candidates);
}

bool fromNode(const vsr::core::DataNode &n, DiscoverDatasetCandidatesResult &r)
{
  return readNodeList(n, "candidates", r.candidates);
}

} // namespace vsr::scivis_studio::protocol
