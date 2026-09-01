// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "PayloadCommon.h"
#include "ProjectRequests.h"
#include "StudioCodec.h"
// std
#include <filesystem>
#include <string>
#include <vector>

using namespace vsr::scivis_studio::protocol;
using vsr::io::ImporterType;

namespace {

// Round-trips a message-level payload through the codec and returns the copy.
template <typename T>
T roundTrip(const T &payload)
{
  const auto msg = encode(payload);
  REQUIRE(msg.header.type == uint8_t(T::MESSAGE_TYPE));
  const auto out = decode<T>(msg);
  REQUIRE(out);
  return *out;
}

// Round-trips a result payload (no MESSAGE_TYPE) through a serialized DataTree.
template <typename T>
T roundTripTree(const T &payload)
{
  vsr::core::DataTree tree;
  toNode(payload, tree.root());
  vsr::network::MessagePayload bytes;
  tree.write(bytes);
  vsr::core::DataTree copy;
  REQUIRE(copy.read(bytes));
  T out;
  REQUIRE(fromNode(copy.root(), out));
  return out;
}

const std::filesystem::path NON_ASCII_PATH = std::filesystem::path(
    "/data/r\xC3\xA9sultats/\xE6\xB8\xA9\xE5\xBA\xA6.vsr");

} // namespace

SCENARIO("Project request payloads", "[StudioProtocol]")
{
  GIVEN("NewProject")
  {
    NewProject req;
    req.requestId = 41;
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 41);

    THEN("a missing requestId is rejected")
    {
      vsr::core::DataTree tree;
      NewProject bad;
      REQUIRE_FALSE(fromNode(tree.root(), bad));
    }
  }

  GIVEN("OpenProject")
  {
    OpenProject req;
    req.requestId = 42;
    req.directory = NON_ASCII_PATH;
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 42);
    REQUIRE(out.directory == NON_ASCII_PATH);

    THEN("a missing directory is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      OpenProject bad;
      REQUIRE_FALSE(fromNode(tree.root(), bad));
    }
  }

  GIVEN("SaveProject with a directory and UI state")
  {
    SaveProject req;
    req.requestId = 43;
    req.directory = std::filesystem::path("/projects/demo");
    req.uiState = makeSubtree();
    req.uiState->root()["windows"]["viewport"]["open"] = true;
    req.uiState->root()["layout"] = std::string("[Window][Viewport]");
    req.uiState->root()["settings"]["theme"] = std::string("dark");

    THEN("everything round-trips, including the opaque subtree")
    {
      const auto out = roundTrip(req);
      REQUIRE(out.requestId == 43);
      REQUIRE(out.directory);
      REQUIRE(*out.directory == std::filesystem::path("/projects/demo"));
      REQUIRE(out.uiState);
      const auto &ui = out.uiState->root();
      REQUIRE(
          readChildOr(*ui.child("windows")->child("viewport"), "open", false));
      REQUIRE(readChildOr(ui, "layout", std::string()) == "[Window][Viewport]");
      REQUIRE(
          readChildOr(*ui.child("settings"), "theme", std::string()) == "dark");
    }
  }

  GIVEN("SaveProject with no directory and no UI state")
  {
    SaveProject req;
    req.requestId = 44;
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 44);
    REQUIRE_FALSE(out.directory);
    REQUIRE_FALSE(out.uiState);

    THEN("a mistyped directory is rejected rather than ignored")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      writeChild(tree.root(), "directory", 7);
      SaveProject bad;
      REQUIRE_FALSE(fromNode(tree.root(), bad));
    }
  }
}

SCENARIO("Dataset creation request payloads", "[StudioProtocol]")
{
  GIVEN("ImportStaticDataset")
  {
    ImportStaticDataset req;
    req.requestId = 50;
    req.name = "wing";
    req.sourcePath = NON_ASCII_PATH;
    req.importerType = ImporterType::OBJ;
    req.fromSubtreeArchive = true;

    THEN("it round-trips with the importer type as a string")
    {
      const auto msg = encode(req);
      vsr::core::DataTree tree;
      REQUIRE(tree.read(msg.payload));
      REQUIRE(readChildOr(tree.root(), "importerType", std::string()) == "OBJ");

      const auto out = roundTrip(req);
      REQUIRE(out.requestId == 50);
      REQUIRE(out.name == "wing");
      REQUIRE(out.sourcePath == NON_ASCII_PATH);
      REQUIRE(out.importerType == ImporterType::OBJ);
      REQUIRE(out.fromSubtreeArchive);
    }

    THEN("the default importer type NONE round-trips")
    {
      req.importerType = ImporterType::NONE;
      req.fromSubtreeArchive = false;
      const auto out = roundTrip(req);
      REQUIRE(out.importerType == ImporterType::NONE);
      REQUIRE_FALSE(out.fromSubtreeArchive);
    }

    THEN("an unknown importer type name is rejected")
    {
      vsr::core::DataTree tree;
      toNode(req, tree.root());
      tree.root()["importerType"] = std::string("NOT_AN_IMPORTER");
      ImportStaticDataset bad;
      REQUIRE_FALSE(fromNode(tree.root(), bad));
    }

    THEN("a missing name or sourcePath is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      writeChild(tree.root(), "importerType", std::string("OBJ"));
      ImportStaticDataset bad;
      REQUIRE_FALSE(fromNode(tree.root(), bad));
      writeChild(tree.root(), "name", std::string("n"));
      REQUIRE_FALSE(fromNode(tree.root(), bad));
      writePath(tree.root(), "sourcePath", "/x.obj");
      REQUIRE(fromNode(tree.root(), bad));
    }
  }

  GIVEN("ImportFileAnimationDataset")
  {
    ImportFileAnimationDataset req;
    req.requestId = 51;
    req.name = "flow";
    req.sourcePaths = {std::filesystem::path("/data/flow_0000.vtu"),
        std::filesystem::path("/data/flow_0001.vtu"),
        NON_ASCII_PATH};
    req.importerType = ImporterType::GLTF;
    req.setActiveShotFrameCount = false;

    THEN("paths keep their order")
    {
      const auto out = roundTrip(req);
      REQUIRE(out.requestId == 51);
      REQUIRE(out.name == "flow");
      REQUIRE(out.sourcePaths == req.sourcePaths);
      REQUIRE(out.importerType == ImporterType::GLTF);
      REQUIRE_FALSE(out.setActiveShotFrameCount);
    }

    THEN("an empty path list round-trips as empty")
    {
      req.sourcePaths.clear();
      const auto out = roundTrip(req);
      REQUIRE(out.sourcePaths.empty());
    }
  }

  GIVEN("DeclareFileAnimationDataset")
  {
    DeclareFileAnimationDataset req;
    req.requestId = 52;
    req.name = "declared";
    req.sourceList = {"a_%04d.vsr", "b_*.vsr"};
    req.importerType = ImporterType::HDRI;
    req.setActiveShotFrameCount = true;

    THEN("it round-trips")
    {
      const auto out = roundTrip(req);
      REQUIRE(out.requestId == 52);
      REQUIRE(out.name == "declared");
      REQUIRE(out.sourceList == req.sourceList);
      REQUIRE(out.importerType == ImporterType::HDRI);
      REQUIRE(out.setActiveShotFrameCount);
    }

    THEN("an empty source list round-trips as empty")
    {
      req.sourceList.clear();
      const auto out = roundTrip(req);
      REQUIRE(out.sourceList.empty());
    }
  }
}

SCENARIO("Dataset edit request payloads", "[StudioProtocol]")
{
  GIVEN("the id-only requests")
  {
    ReimportDataset reimport;
    reimport.requestId = 60;
    reimport.datasetId = "ds-3";
    auto out1 = roundTrip(reimport);
    REQUIRE(out1.requestId == 60);
    REQUIRE(out1.datasetId == "ds-3");

    LoadDataset load;
    load.requestId = 61;
    load.datasetId = "ds-4";
    auto out2 = roundTrip(load);
    REQUIRE(out2.requestId == 61);
    REQUIRE(out2.datasetId == "ds-4");

    UnloadDataset unload;
    unload.requestId = 62;
    unload.datasetId = "ds-5";
    auto out3 = roundTrip(unload);
    REQUIRE(out3.requestId == 62);
    REQUIRE(out3.datasetId == "ds-5");

    RefreshDatasetAvailability refresh;
    refresh.requestId = 63;
    refresh.datasetId = "ds-6";
    auto out4 = roundTrip(refresh);
    REQUIRE(out4.requestId == 63);
    REQUIRE(out4.datasetId == "ds-6");

    THEN("they are distinguished by type byte")
    {
      const auto msg = encode(load);
      REQUIRE_FALSE(decode<UnloadDataset>(msg));
      REQUIRE_FALSE(decode<ReimportDataset>(msg));
      REQUIRE(decode<LoadDataset>(msg));
    }
  }

  GIVEN("RenameDataset")
  {
    RenameDataset req;
    req.requestId = 64;
    req.datasetId = "ds-7";
    req.newName = "pressure";
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 64);
    REQUIRE(out.datasetId == "ds-7");
    REQUIRE(out.newName == "pressure");

    THEN("decode fails when datasetId is missing")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(64));
      writeChild(tree.root(), "newName", std::string("pressure"));
      vsr::network::Message msg;
      msg.header.type = uint8_t(StudioMessageType::RenameDataset);
      tree.write(msg.payload);
      msg.header.payload_length = uint32_t(msg.payload.size());
      REQUIRE_FALSE(decode<RenameDataset>(msg));
    }
  }

  GIVEN("RemoveDataset")
  {
    RemoveDataset req;
    req.requestId = 65;
    req.datasetId = "ds-8";
    req.keepAssetFile = true;
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 65);
    REQUIRE(out.datasetId == "ds-8");
    REQUIRE(out.keepAssetFile);

    THEN("keepAssetFile defaults to false when absent")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      writeChild(tree.root(), "datasetId", std::string("ds-8"));
      RemoveDataset bare;
      bare.keepAssetFile = true;
      REQUIRE(fromNode(tree.root(), bare));
      REQUIRE_FALSE(bare.keepAssetFile);
    }
  }
}

SCENARIO("Dataset archive and candidate request payloads", "[StudioProtocol]")
{
  GIVEN("SaveDatasetArchive")
  {
    SaveDatasetArchive req;
    req.requestId = 70;
    req.datasetId = "ds-9";
    req.file = NON_ASCII_PATH;
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 70);
    REQUIRE(out.datasetId == "ds-9");
    REQUIRE(out.file == NON_ASCII_PATH);
  }

  GIVEN("LoadDatasetArchive")
  {
    LoadDatasetArchive req;
    req.requestId = 71;
    req.file = std::filesystem::path("/archives/wing.vsr");
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 71);
    REQUIRE(out.file == req.file);
  }

  GIVEN("DiscoverDatasetCandidates")
  {
    DiscoverDatasetCandidates req;
    req.requestId = 72;
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 72);
  }

  GIVEN("IncorporateDatasetCandidate")
  {
    IncorporateDatasetCandidate req;
    req.requestId = 73;
    req.file = NON_ASCII_PATH;
    req.proposedName = "temperature";
    req.name = "Temperature (K)";
    const auto out = roundTrip(req);
    REQUIRE(out.requestId == 73);
    REQUIRE(out.file == NON_ASCII_PATH);
    REQUIRE(out.proposedName == "temperature");
    REQUIRE(out.name == "Temperature (K)");

    THEN("an empty proposedName round-trips")
    {
      req.proposedName.clear();
      const auto again = roundTrip(req);
      REQUIRE(again.proposedName.empty());
    }
  }
}

SCENARIO("Project result payloads", "[StudioProtocol]")
{
  GIVEN("DatasetCreatedResult")
  {
    DatasetCreatedResult res;
    res.datasetId = "ds-10";
    const auto out = roundTripTree(res);
    REQUIRE(out.datasetId == "ds-10");

    THEN("a missing datasetId is rejected")
    {
      vsr::core::DataTree tree;
      DatasetCreatedResult bad;
      REQUIRE_FALSE(fromNode(tree.root(), bad));
    }
  }

  GIVEN("DiscoverDatasetCandidatesResult")
  {
    DiscoverDatasetCandidatesResult res;
    res.candidates.push_back({std::filesystem::path("/p/assets/a.vsr"), "a"});
    res.candidates.push_back({NON_ASCII_PATH, ""});
    res.candidates.push_back({std::filesystem::path("/p/assets/c.vsr"), "c"});

    THEN("entries round-trip in order")
    {
      const auto out = roundTripTree(res);
      REQUIRE(out.candidates.size() == 3);
      REQUIRE(out.candidates[0].file == res.candidates[0].file);
      REQUIRE(out.candidates[0].proposedName == "a");
      REQUIRE(out.candidates[1].file == NON_ASCII_PATH);
      REQUIRE(out.candidates[1].proposedName.empty());
      REQUIRE(out.candidates[2].proposedName == "c");
    }

    THEN("an empty list round-trips as empty")
    {
      res.candidates.clear();
      const auto out = roundTripTree(res);
      REQUIRE(out.candidates.empty());
    }

    THEN("an entry without a file is rejected")
    {
      vsr::core::DataTree tree;
      tree.root()["candidates"]["0"]["proposedName"] = std::string("x");
      DiscoverDatasetCandidatesResult bad;
      REQUIRE_FALSE(fromNode(tree.root(), bad));
    }
  }
}

SCENARIO("Importer type names", "[StudioProtocol]")
{
  GIVEN("the model's importer enum")
  {
    THEN("every value round-trips through its name")
    {
      for (int i = 0; i <= int(ImporterType::NONE); ++i) {
        const auto type = ImporterType(i);
        const auto parsed = importerTypeFromString(toString(type));
        REQUIRE(parsed);
        REQUIRE(*parsed == type);
      }
    }

    THEN("unknown names read as empty rather than NONE")
    {
      REQUIRE_FALSE(importerTypeFromString(""));
      REQUIRE_FALSE(importerTypeFromString("obj"));
      REQUIRE_FALSE(importerTypeFromString("NOT_AN_IMPORTER"));
      REQUIRE(importerTypeFromString("NONE") == ImporterType::NONE);
    }
  }
}
