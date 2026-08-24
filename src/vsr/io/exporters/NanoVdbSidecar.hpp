// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/core/VSRMath.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vsr::io {

struct NanoVdbSidecar
{
  int schemaVersion = 1;
  std::string volumeType;
  std::string dataCentering;
  std::optional<math::float3> origin;
  std::optional<math::float3> spacing;
  std::optional<math::box3> roi;
  std::vector<double> coordsX;
  std::vector<double> coordsY;
  std::vector<double> coordsZ;

  bool hasCoords() const;
};

// Inlined definitions ////////////////////////////////////////////////////////

inline bool NanoVdbSidecar::hasCoords() const
{
  return !coordsX.empty() && !coordsY.empty() && !coordsZ.empty();
}

std::filesystem::path makeSidecarPath(const std::filesystem::path &nvdbPath);

bool writeSidecar(const NanoVdbSidecar &sidecar,
    const std::filesystem::path &path,
    std::string &errorMessage);

std::optional<NanoVdbSidecar> readSidecar(
    const std::filesystem::path &path, std::string &errorMessage);

} // namespace vsr::io
