// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ViewportMessages.h"
#include "PayloadCommon.h"

namespace vsr::scivis_studio::protocol {

using vsr::rendering::AOVType;

namespace {

void writeOptionalObject(vsr::core::DataNode &parent,
    const char *name,
    const std::optional<SceneObjectRef> &object)
{
  if (object)
    writeChildNode(parent, name, *object);
}

bool readOptionalObject(const vsr::core::DataNode &parent,
    const char *name,
    std::optional<SceneObjectRef> &out)
{
  out.reset();
  if (!hasChild(parent, name))
    return true;
  SceneObjectRef ref;
  if (!readChildNode(parent, name, ref))
    return false;
  out = ref;
  return true;
}

} // namespace

const char *toString(AOVType type)
{
  switch (type) {
  case AOVType::NONE:
    return "NONE";
  case AOVType::DEPTH:
    return "DEPTH";
  case AOVType::ALBEDO:
    return "ALBEDO";
  case AOVType::NORMAL:
    return "NORMAL";
  case AOVType::EDGES:
    return "EDGES";
  case AOVType::OBJECT_ID:
    return "OBJECT_ID";
  case AOVType::PRIMITIVE_ID:
    return "PRIMITIVE_ID";
  case AOVType::INSTANCE_ID:
    return "INSTANCE_ID";
  }
  return "NONE";
}

std::optional<AOVType> aovTypeFromString(std::string_view name)
{
  if (name == "NONE")
    return AOVType::NONE;
  if (name == "DEPTH")
    return AOVType::DEPTH;
  if (name == "ALBEDO")
    return AOVType::ALBEDO;
  if (name == "NORMAL")
    return AOVType::NORMAL;
  if (name == "EDGES")
    return AOVType::EDGES;
  if (name == "OBJECT_ID")
    return AOVType::OBJECT_ID;
  if (name == "PRIMITIVE_ID")
    return AOVType::PRIMITIVE_ID;
  if (name == "INSTANCE_ID")
    return AOVType::INSTANCE_ID;
  return {};
}

// Picking ////////////////////////////////////////////////////////////////////

void toNode(const Pick &p, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", p.requestId);
  writeChild(n, "x", p.x);
  writeChild(n, "y", p.y);
}

bool fromNode(const vsr::core::DataNode &n, Pick &p)
{
  return readChild(n, "requestId", p.requestId) && readChild(n, "x", p.x)
      && readChild(n, "y", p.y);
}

void toNode(const PickReply &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChild(n, "hit", r.hit);
  writeChild(n, "worldPosition", r.worldPosition);
  writeOptionalObject(n, "object", r.object);
}

bool fromNode(const vsr::core::DataNode &n, PickReply &r)
{
  if (!readChild(n, "requestId", r.requestId) || !readChild(n, "hit", r.hit))
    return false;
  r.worldPosition = vsr::math::float3(0.f, 0.f, 0.f);
  return readOptionalChild(n, "worldPosition", r.worldPosition)
      && readOptionalObject(n, "object", r.object);
}

void toNode(const SetOutline &o, vsr::core::DataNode &n)
{
  writeOptionalObject(n, "object", o.object);
}

bool fromNode(const vsr::core::DataNode &n, SetOutline &o)
{
  return readOptionalObject(n, "object", o.object);
}

// Viewport passes ////////////////////////////////////////////////////////////

void toNode(const ViewportSettings &s, vsr::core::DataNode &n)
{
  writeChild(n, "highlightSelection", s.highlightSelection);
  writeChild(n, "outlinePrimitives", s.outlinePrimitives);
  writeChild(n, "showWorldBounds", s.showWorldBounds);
  writeChild(n, "worldBoundsColor", s.worldBoundsColor);
  writeChild(n, "worldBoundsWidth", s.worldBoundsWidth);
  writeChild(n, "visualizeAOV", std::string(toString(s.visualizeAOV)));
  writeChild(n, "depthVisualMinimum", s.depthVisualMinimum);
  writeChild(n, "depthVisualMaximum", s.depthVisualMaximum);
  writeChild(n, "edgeInvert", s.edgeInvert);
}

bool fromNode(const vsr::core::DataNode &n, ViewportSettings &s)
{
  s = ViewportSettings{};
  return readOptionalEnumChild(
             n, "visualizeAOV", s.visualizeAOV, aovTypeFromString)
      && readOptionalChild(n, "highlightSelection", s.highlightSelection)
      && readOptionalChild(n, "outlinePrimitives", s.outlinePrimitives)
      && readOptionalChild(n, "showWorldBounds", s.showWorldBounds)
      && readOptionalChild(n, "worldBoundsColor", s.worldBoundsColor)
      && readOptionalChild(n, "worldBoundsWidth", s.worldBoundsWidth)
      && readOptionalChild(n, "depthVisualMinimum", s.depthVisualMinimum)
      && readOptionalChild(n, "depthVisualMaximum", s.depthVisualMaximum)
      && readOptionalChild(n, "edgeInvert", s.edgeInvert);
}

// Histogram //////////////////////////////////////////////////////////////////

void toNode(const RequestArrayHistogram &r, vsr::core::DataNode &n)
{
  writeChild(n, "requestId", r.requestId);
  writeChildNode(n, "array", r.array);
  writeChild(n, "binCount", r.binCount);
}

bool fromNode(const vsr::core::DataNode &n, RequestArrayHistogram &r)
{
  return readChild(n, "requestId", r.requestId)
      && readChildNode(n, "array", r.array)
      && readChild(n, "binCount", r.binCount);
}

void toNode(const ArrayHistogramResult &r, vsr::core::DataNode &n)
{
  if (!r.bins.empty())
    n["bins"].setValueAsArray(r.bins);
  writeChild(n, "minValue", r.minValue);
  writeChild(n, "maxValue", r.maxValue);
}

bool fromNode(const vsr::core::DataNode &n, ArrayHistogramResult &r)
{
  if (!readChild(n, "minValue", r.minValue)
      || !readChild(n, "maxValue", r.maxValue))
    return false;
  r.bins.clear();
  const auto *bins = n.child("bins");
  if (!bins)
    return true;
  const uint64_t *data = nullptr;
  size_t count = 0;
  bins->getValueAsArray(&data, &count);
  if (!data)
    return false;
  r.bins.assign(data, data + count);
  return true;
}

} // namespace vsr::scivis_studio::protocol
