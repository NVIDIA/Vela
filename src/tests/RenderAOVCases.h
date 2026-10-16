// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_rendering
#include "vsr/rendering/pipeline/passes/AnariSceneRenderPass.h"
#include "vsr/rendering/pipeline/passes/VisualizeAOVPass.h"

namespace vsr::tests {

struct OptionalAOVCase
{
  rendering::AOVType type;
  const char *mode;
  const char *channel;
  const char *extension;
};

inline constexpr OptionalAOVCase OPTIONAL_AOV_CASES[] = {
    {rendering::AOVType::ALBEDO,
        "albedo",
        "channel.albedo",
        "ANARI_KHR_FRAME_CHANNEL_ALBEDO"},
    {rendering::AOVType::NORMAL,
        "normal",
        "channel.normal",
        "ANARI_KHR_FRAME_CHANNEL_NORMAL"},
    {rendering::AOVType::OBJECT_ID,
        "object-id",
        "channel.objectId",
        "ANARI_KHR_FRAME_CHANNEL_OBJECT_ID"},
    {rendering::AOVType::PRIMITIVE_ID,
        "primitive-id",
        "channel.primitiveId",
        "ANARI_KHR_FRAME_CHANNEL_PRIMITIVE_ID"},
    {rendering::AOVType::INSTANCE_ID,
        "instance-id",
        "channel.instanceId",
        "ANARI_KHR_FRAME_CHANNEL_INSTANCE_ID"}};

inline const OptionalAOVCase *unsupportedAOV(anari::Device device)
{
  for (const auto &aov : OPTIONAL_AOV_CASES) {
    if (!rendering::deviceSupportsExtension(device, aov.extension))
      return &aov;
  }
  return nullptr;
}

} // namespace vsr::tests
