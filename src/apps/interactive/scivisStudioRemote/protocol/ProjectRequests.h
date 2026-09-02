// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "PayloadCommon.h"
#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// vsr_io
#include "vsr/io/importers.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_network
#include "vsr/network/Message.hpp"
// std
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vsr::scivis_studio::protocol {

/*
 * Client->server project and dataset requests, mirroring ProjectContext's
 * project/dataset operations one-to-one. Every request carries a client-chosen
 * requestId as its first field; the server answers with a ProjectOpReply
 * bearing the same id (sync ops) or a TaskStartedResult (task ops). Paths are
 * absolute server-side paths (no file bytes cross the wire), and datasets are
 * addressed by their string DatasetID.
 *
 * Example:
 *   RenameDataset req;
 *   req.requestId = nextRequestId();
 *   req.datasetId = dataset.id;
 *   req.newName = "pressure";
 *   send(encode(req));
 */

// Project ////////////////////////////////////////////////////////////////////

// Sync: replaces the current project with an unsaved empty one.
struct NewProject
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::NewProject;
  uint64_t requestId{0};
};

// Task: opens the project stored in `directory`.
struct OpenProject
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::OpenProject;
  uint64_t requestId{0};
  std::filesystem::path directory;
};

// Task: saves to `directory`, or to the project's own directory when absent.
// `uiState` (windows, layout, settings) is opaque to the server; it is stored
// with the project and handed back on open.
struct SaveProject
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SaveProject;
  uint64_t requestId{0};
  std::optional<std::filesystem::path> directory;
  SubtreePtr uiState;
};

// Dataset creation ///////////////////////////////////////////////////////////

// Task: addStaticDataset(), or addStaticDatasetFromSubtree() when
// `fromSubtreeArchive` is set (then `importerType` is ignored).
struct ImportStaticDataset
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::ImportStaticDataset;
  uint64_t requestId{0};
  std::string name;
  std::filesystem::path sourcePath;
  vsr::io::ImporterType importerType{vsr::io::ImporterType::NONE};
  bool fromSubtreeArchive{false};
};

// Task: addFileAnimationDataset().
struct ImportFileAnimationDataset
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::ImportFileAnimationDataset;
  uint64_t requestId{0};
  std::string name;
  std::vector<std::filesystem::path> sourcePaths;
  vsr::io::ImporterType importerType{vsr::io::ImporterType::NONE};
  bool setActiveShotFrameCount{true};
};

// Sync: addDeclaredFileAnimationDataset() -- reads nothing from disk, so the
// reply's results decode to DatasetCreatedResult.
struct DeclareFileAnimationDataset
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::DeclareFileAnimationDataset;
  uint64_t requestId{0};
  std::string name;
  std::vector<std::string> sourceList;
  vsr::io::ImporterType importerType{vsr::io::ImporterType::NONE};
  bool setActiveShotFrameCount{true};
};

// Dataset edits and residency ////////////////////////////////////////////////

// Task: reimportStaticDataset().
struct ReimportDataset
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::ReimportDataset;
  uint64_t requestId{0};
  DatasetID datasetId;
};

// Sync: renameDataset().
struct RenameDataset
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RenameDataset;
  uint64_t requestId{0};
  DatasetID datasetId;
  std::string newName;
};

// Sync: removeDataset().
struct RemoveDataset
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RemoveDataset;
  uint64_t requestId{0};
  DatasetID datasetId;
  bool keepAssetFile{false};
};

// Task: loadDataset().
struct LoadDataset
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::LoadDataset;
  uint64_t requestId{0};
  DatasetID datasetId;
};

// Sync: unloadDataset().
struct UnloadDataset
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::UnloadDataset;
  uint64_t requestId{0};
  DatasetID datasetId;
};

// Sync: refreshUnloadedDatasetAvailability() on the dataset with this id.
struct RefreshDatasetAvailability
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RefreshDatasetAvailability;
  uint64_t requestId{0};
  DatasetID datasetId;
};

// Dataset archives and candidates ////////////////////////////////////////////

// Task: saveDatasetArchive().
struct SaveDatasetArchive
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SaveDatasetArchive;
  uint64_t requestId{0};
  DatasetID datasetId;
  std::filesystem::path file;
};

// Task: loadDatasetArchive().
struct LoadDatasetArchive
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::LoadDatasetArchive;
  uint64_t requestId{0};
  std::filesystem::path file;
};

// Sync: discoverDatasetCandidates(); the reply's results decode to
// DiscoverDatasetCandidatesResult.
struct DiscoverDatasetCandidates
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::DiscoverDatasetCandidates;
  uint64_t requestId{0};
};

// Task: incorporateDatasetCandidate({file, proposedName}, name).
struct IncorporateDatasetCandidate
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::IncorporateDatasetCandidate;
  uint64_t requestId{0};
  std::filesystem::path file;
  std::string proposedName;
  std::string name;
};

// Results (carried in ProjectOpReply::results, never sent alone) ////////////

// The id the server allocated for a synchronously created dataset.
struct DatasetCreatedResult
{
  DatasetID datasetId;
};

// One discovered-but-unregistered asset file in the project directory.
struct DatasetCandidateEntry
{
  std::filesystem::path file;
  std::string proposedName;
};

struct DiscoverDatasetCandidatesResult
{
  std::vector<DatasetCandidateEntry> candidates;
};

// Request plumbing ///////////////////////////////////////////////////////////

// The requestId a request payload carries as its first field, read without
// knowing the payload type, so a receiver can still answer a request whose
// remaining fields it cannot decode (or a type it does not serve) with a
// ProjectOpReply the sender can match. Empty when the payload has no
// requestId or does not parse at all.
std::optional<uint64_t> peekRequestId(const vsr::network::Message &msg);

// Importer type names ////////////////////////////////////////////////////////

// Wire form of vsr::io::ImporterType ("OBJ", "GLTF", ..., "NONE"); an
// unrecognised name reads as empty.
const char *toString(vsr::io::ImporterType importerType);
std::optional<vsr::io::ImporterType> importerTypeFromString(
    const std::string &name);

// Serialization //////////////////////////////////////////////////////////////

// requestId is required on every request. Ids, names, paths and enum fields
// are required unless noted; collections and optionals may be absent.

void toNode(const NewProject &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, NewProject &);

void toNode(const OpenProject &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, OpenProject &);

// directory and uiState are optional.
void toNode(const SaveProject &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SaveProject &);

// fromSubtreeArchive is optional (defaults false).
void toNode(const ImportStaticDataset &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ImportStaticDataset &);

// setActiveShotFrameCount is optional (defaults true).
void toNode(const ImportFileAnimationDataset &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ImportFileAnimationDataset &);
void toNode(const DeclareFileAnimationDataset &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, DeclareFileAnimationDataset &);

void toNode(const ReimportDataset &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ReimportDataset &);
void toNode(const RenameDataset &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RenameDataset &);

// keepAssetFile is optional (defaults false).
void toNode(const RemoveDataset &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RemoveDataset &);

void toNode(const LoadDataset &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, LoadDataset &);
void toNode(const UnloadDataset &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, UnloadDataset &);
void toNode(const RefreshDatasetAvailability &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RefreshDatasetAvailability &);

void toNode(const SaveDatasetArchive &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SaveDatasetArchive &);
void toNode(const LoadDatasetArchive &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, LoadDatasetArchive &);
void toNode(const DiscoverDatasetCandidates &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, DiscoverDatasetCandidates &);

// proposedName is optional (defaults "").
void toNode(const IncorporateDatasetCandidate &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, IncorporateDatasetCandidate &);

void toNode(const DatasetCreatedResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, DatasetCreatedResult &);

// file is required; proposedName is optional.
void toNode(const DatasetCandidateEntry &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, DatasetCandidateEntry &);

// Candidates live under children "0", "1", ...; an absent list reads as empty.
void toNode(const DiscoverDatasetCandidatesResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, DiscoverDatasetCandidatesResult &);

} // namespace vsr::scivis_studio::protocol
