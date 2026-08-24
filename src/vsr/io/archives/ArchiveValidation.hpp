// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>

namespace vsr::io {

enum class ArchiveValidationStatus
{
  Valid,
  MissingMetadataAccepted,
  UnknownSchema,
  IncompatibleSchema,
  UnsupportedEnvelopeVersion,
  UnsupportedSchemaVersion,
  MalformedMetadata,
  MissingRequiredNode
};

struct ArchiveValidationResult
{
  ArchiveValidationStatus status{ArchiveValidationStatus::Valid};
  std::string fileType;
  std::string schema;
  int envelopeVersion{0};
  int schemaVersion{0};
  std::string message;

  bool accepted() const;
};

inline bool ArchiveValidationResult::accepted() const
{
  return status == ArchiveValidationStatus::Valid
      || status == ArchiveValidationStatus::MissingMetadataAccepted;
}

} // namespace vsr::io
