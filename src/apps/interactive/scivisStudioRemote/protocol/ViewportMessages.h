// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "StudioProtocol.h"
// vsr_scivis_studio_model
#include "Dataset.h"
// vsr_rendering
#include "vsr/rendering/pipeline/passes/VisualizeAOVPass.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
#include "vsr/core/VSRMath.hpp"
// std
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace vsr::scivis_studio::protocol {

/*
 * Picking, selection outline, viewport passes and the array histogram. The
 * server picks against its current camera and scene and answers with one
 * flat PickReply; the client owns selection and tells the server only what
 * rendering needs (SetOutline). ViewportSettings is the flat, optimistic
 * mirror of the monolith Viewport's id-driven pass toggles, which composite
 * server-side before encoding in v1.
 *
 * Example:
 *   Pick pick;
 *   pick.requestId = nextId();
 *   pick.x = mouse.x;
 *   pick.y = mouse.y;
 *   channel.send(encode(pick));
 */

struct Pick
{
  static constexpr StudioMessageType kType = StudioMessageType::Pick;
  uint64_t requestId{0};
  int x{0};
  int y{0};
};

// object is absent when the pick landed on background.
struct PickReply
{
  static constexpr StudioMessageType kType = StudioMessageType::PickReply;
  uint64_t requestId{0};
  bool hit{false};
  vsr::math::float3 worldPosition{0.f, 0.f, 0.f};
  std::optional<SceneObjectRef> object;
};

// Empty object clears the outline.
struct SetOutline
{
  static constexpr StudioMessageType kType = StudioMessageType::SetOutline;
  std::optional<SceneObjectRef> object;
};

// Field names and defaults mirror vsr::ui::Viewport's saved settings so the
// two stay one vocabulary: highlightSelection drives OutlineRenderPass,
// outlinePrimitives PrimitiveOutlineRenderPass, showWorldBounds (+ color,
// width) BoxOutlineRenderPass, and visualizeAOV (+ depth range, edgeInvert)
// VisualizeAOVPass.
struct ViewportSettings
{
  static constexpr StudioMessageType kType =
      StudioMessageType::ViewportSettings;
  bool highlightSelection{true};
  bool outlinePrimitives{false};
  bool showWorldBounds{false};
  vsr::math::float4 worldBoundsColor{0.8f, 0.8f, 0.8f, 1.f};
  int worldBoundsWidth{1};
  vsr::rendering::AOVType visualizeAOV{vsr::rendering::AOVType::NONE};
  float depthVisualMinimum{0.f};
  float depthVisualMaximum{1.f};
  bool edgeInvert{false};
};

struct RequestArrayHistogram
{
  static constexpr StudioMessageType kType =
      StudioMessageType::RequestArrayHistogram;
  uint64_t requestId{0};
  SceneObjectRef array;
  uint32_t binCount{0};
};

// Result carried in a ProjectOpReply's `results` subtree.
struct ArrayHistogramResult
{
  std::vector<uint64_t> bins;
  float minValue{0.f};
  float maxValue{0.f};
};

// Enumerator names ("NONE", "DEPTH", ..., "INSTANCE_ID").
const char *toString(vsr::rendering::AOVType type);
std::optional<vsr::rendering::AOVType> aovTypeFromString(std::string_view name);

// requestId, x and y are required.
void toNode(const Pick &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, Pick &);

// requestId and hit are required; worldPosition defaults to the origin; a
// present but malformed object is rejected.
void toNode(const PickReply &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, PickReply &);

// An absent object reads as "clear"; a present but malformed one is rejected.
void toNode(const SetOutline &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, SetOutline &);

// Every field is optional and keeps its default when absent, so a newer
// client can add toggles without breaking an older server; a present but
// mistyped field is rejected.
void toNode(const ViewportSettings &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ViewportSettings &);

// requestId, array and binCount are required.
void toNode(const RequestArrayHistogram &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, RequestArrayHistogram &);

// bins travel as one UINT64 array leaf (absent when empty); minValue and
// maxValue are required.
void toNode(const ArrayHistogramResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ArrayHistogramResult &);

} // namespace vsr::scivis_studio::protocol
