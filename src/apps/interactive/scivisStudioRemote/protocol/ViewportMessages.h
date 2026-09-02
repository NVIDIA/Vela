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

// x and y are frame pixels in the header's width x height: x grows to the
// right, y downwards from the top-left corner (the client's image origin;
// the server converts to ANARI's bottom-up buffer). Coordinates outside the
// frame are clamped to its edge. The server services one Pick at a time,
// latest-wins: a Pick arriving before an earlier one was serviced replaces
// it, and only the survivor is answered.
struct Pick
{
  static constexpr StudioMessageType MESSAGE_TYPE = StudioMessageType::Pick;
  uint64_t requestId{0};
  int x{0};
  int y{0};
};

// objectIdentity is the server-minted (type, pool index) of what was hit;
// absent when the pick landed on background.
struct PickReply
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::PickReply;
  uint64_t requestId{0};
  bool hit{false};
  vsr::math::float3 worldPosition{0.f, 0.f, 0.f};
  std::optional<SceneObjectRef> objectIdentity;
};

// An absent objectIdentity clears the outline.
struct SetOutline
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::SetOutline;
  std::optional<SceneObjectRef> objectIdentity;
};

// Field names and defaults mirror vsr::ui::Viewport's saved settings so the
// two stay one vocabulary: highlightSelection drives OutlineRenderPass,
// outlinePrimitives PrimitiveOutlineRenderPass, showWorldBounds (+ color,
// width) BoxOutlineRenderPass, and visualizeAOV (+ depth range, edgeInvert)
// VisualizeAOVPass.
struct ViewportSettings
{
  static constexpr StudioMessageType MESSAGE_TYPE =
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

// The server clamps binCount into this range before binning; both ends of
// the wire agree on it.
constexpr uint32_t MIN_HISTOGRAM_BINS = 1;
constexpr uint32_t MAX_HISTOGRAM_BINS = 4096;

struct RequestArrayHistogram
{
  static constexpr StudioMessageType MESSAGE_TYPE =
      StudioMessageType::RequestArrayHistogram;
  uint64_t requestId{0};
  SceneObjectRef array;
  uint32_t binCount{0};
};

// Result carried in a ProjectOpReply's `results` subtree. Elements that are
// NaN or infinite take no part in the range or the bins; nonFinite counts
// them so the bins still account for every element.
struct ArrayHistogramResult
{
  std::vector<uint64_t> bins;
  float minValue{0.f};
  float maxValue{0.f};
  uint64_t nonFinite{0};
};

// Enumerator names ("NONE", "DEPTH", ..., "INSTANCE_ID"), "Unknown" otherwise.
const char *toString(vsr::rendering::AOVType type);
std::optional<vsr::rendering::AOVType> aovTypeFromString(std::string_view name);

// requestId, x and y are required.
void toNode(const Pick &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, Pick &);

// requestId and hit are required; worldPosition defaults to the origin; a
// present but malformed objectIdentity is rejected.
void toNode(const PickReply &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, PickReply &);

// An absent objectIdentity reads as "clear"; a present but malformed one is
// rejected.
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
// maxValue are required, nonFinite defaults to 0 when absent.
void toNode(const ArrayHistogramResult &, vsr::core::DataNode &);
bool fromNode(const vsr::core::DataNode &, ArrayHistogramResult &);

} // namespace vsr::scivis_studio::protocol
