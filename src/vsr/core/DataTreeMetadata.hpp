// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/core/DataTree.hpp"
// std
#include <optional>
#include <string>
#include <string_view>

namespace vsr::core {

inline constexpr const char *DATA_TREE_METADATA_NODE = "__vsr_metadata";
// Files written before the TSD -> VSR rename carry this node instead, and spell
// their schemas "tsd.*". Reads accept both; writes only ever emit the VSR form.
inline constexpr const char *LEGACY_DATA_TREE_METADATA_NODE = "__tsd_metadata";
inline constexpr std::string_view LEGACY_SCHEMA_PREFIX = "tsd.";
inline constexpr std::string_view SCHEMA_PREFIX = "vsr.";
inline constexpr int DATA_TREE_METADATA_ENVELOPE_VERSION = 1;

struct DataTreeMetadata
{
  int envelopeVersion{DATA_TREE_METADATA_ENVELOPE_VERSION};
  std::string fileType;
  std::string schema;
  int schemaVersion{1};
};

enum class DataTreeMetadataReadStatus
{
  Found,
  Missing,
  Malformed
};

struct DataTreeMetadataReadResult
{
  DataTreeMetadataReadStatus status{DataTreeMetadataReadStatus::Missing};
  std::optional<DataTreeMetadata> metadata;
  std::string message;

  bool found() const;
  bool malformed() const;
};

inline bool DataTreeMetadataReadResult::found() const
{
  return status == DataTreeMetadataReadStatus::Found;
}

inline bool DataTreeMetadataReadResult::malformed() const
{
  return status == DataTreeMetadataReadStatus::Malformed;
}

inline void writeDataTreeMetadata(
    DataNode &root, const DataTreeMetadata &metadata)
{
  auto &metadataNode = root[DATA_TREE_METADATA_NODE];
  metadataNode["envelopeVersion"] = metadata.envelopeVersion;
  metadataNode["fileType"] = metadata.fileType;
  metadataNode["schema"] = metadata.schema;
  metadataNode["schemaVersion"] = metadata.schemaVersion;
}

// Rewrite a legacy "tsd.*" schema to its "vsr.*" equivalent so callers can
// compare against the current schema constants without knowing about the
// rename. Any other schema is passed through untouched.
inline std::string normalizeLegacySchema(std::string schema)
{
  if (std::string_view(schema).substr(0, LEGACY_SCHEMA_PREFIX.size())
      == LEGACY_SCHEMA_PREFIX) {
    schema.replace(0,
        LEGACY_SCHEMA_PREFIX.size(),
        SCHEMA_PREFIX.data(),
        SCHEMA_PREFIX.size());
  }
  return schema;
}

inline DataTreeMetadataReadResult readDataTreeMetadata(const DataNode &root)
{
  const char *metadataNodeName = DATA_TREE_METADATA_NODE;
  auto *metadataNode = root.child(metadataNodeName);
  if (!metadataNode) {
    metadataNodeName = LEGACY_DATA_TREE_METADATA_NODE;
    metadataNode = root.child(metadataNodeName);
  }
  if (!metadataNode)
    return {};

  auto requiredNode = [&](const char *name, anari::DataType type)
      -> const DataNode * {
    auto *node = metadataNode->child(name);
    if (!node)
      return nullptr;

    const auto actualType = node->getValue().type();
    if (actualType != type)
      return nullptr;

    return node;
  };

  auto malformed = [](std::string message) {
    DataTreeMetadataReadResult result;
    result.status = DataTreeMetadataReadStatus::Malformed;
    result.message = std::move(message);
    return result;
  };

  auto describeMissingOrWrongType =
      [&](const char *name, anari::DataType type) -> std::string {
    std::string message = std::string(metadataNodeName) + "/" + name
        + " must be " + anari::toString(type);
    if (auto *node = metadataNode->child(name)) {
      message += ", got ";
      message += anari::toString(node->getValue().type());
    } else
      message += ", but is missing";
    return message;
  };

  auto *envelopeVersion = requiredNode("envelopeVersion", ANARI_INT32);
  if (!envelopeVersion)
    return malformed(describeMissingOrWrongType("envelopeVersion", ANARI_INT32));

  auto *fileType = requiredNode("fileType", ANARI_STRING);
  if (!fileType)
    return malformed(describeMissingOrWrongType("fileType", ANARI_STRING));

  auto *schema = requiredNode("schema", ANARI_STRING);
  if (!schema)
    return malformed(describeMissingOrWrongType("schema", ANARI_STRING));

  auto *schemaVersion = requiredNode("schemaVersion", ANARI_INT32);
  if (!schemaVersion)
    return malformed(describeMissingOrWrongType("schemaVersion", ANARI_INT32));

  DataTreeMetadata metadata;
  metadata.envelopeVersion = envelopeVersion->getValueAs<int>();
  metadata.fileType = fileType->getValueAs<std::string>();
  metadata.schema = normalizeLegacySchema(schema->getValueAs<std::string>());
  metadata.schemaVersion = schemaVersion->getValueAs<int>();

  DataTreeMetadataReadResult result;
  result.status = DataTreeMetadataReadStatus::Found;
  result.metadata = std::move(metadata);
  return result;
}

} // namespace vsr::core
