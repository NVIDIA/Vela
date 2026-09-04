// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ProjectRequests.h"
// vsr_scivis_studio_model
#include "ProjectContext.h"

namespace vsr::scivis_studio::protocol {

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

} // namespace vsr::scivis_studio::protocol
