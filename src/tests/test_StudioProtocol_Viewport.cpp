// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "BrowseMessages.h"
#include "PayloadCommon.h"
#include "PlaybackMessages.h"
#include "SceneEditMessages.h"
#include "StudioCodec.h"
#include "ViewportMessages.h"
// std
#include <string>
#include <vector>

using namespace vsr::scivis_studio::protocol;
using vsr::scivis_studio::SceneNodeRef;
using vsr::scivis_studio::SceneObjectRef;

namespace {

// Result payloads have no MESSAGE_TYPE; push them through a serialized tree.
template <typename T>
bool roundTripNode(const T &in, T &out)
{
  vsr::core::DataTree tree;
  toNode(in, tree.root());
  vsr::network::MessagePayload bytes;
  tree.write(bytes);
  vsr::core::DataTree copy;
  if (!copy.read(bytes))
    return false;
  return fromNode(copy.root(), out);
}

SceneObjectRef makeRef(anari::DataType type, size_t index)
{
  SceneObjectRef ref;
  ref.type = type;
  ref.objectIndex = index;
  return ref;
}

} // namespace

SCENARIO("Remote Browse payloads", "[StudioProtocol]")
{
  GIVEN("ListRoots and ListDirectory requests")
  {
    ListRoots roots;
    roots.requestId = 11;
    ListDirectory dir;
    dir.requestId = 12;
    dir.directory = "/data/run 1";

    THEN("they round-trip with their requestId")
    {
      const auto r = decode<ListRoots>(encode(roots));
      REQUIRE(r);
      REQUIRE(r->requestId == 11);

      const auto d = decode<ListDirectory>(encode(dir));
      REQUIRE(d);
      REQUIRE(d->requestId == 12);
      REQUIRE(d->directory == std::filesystem::path("/data/run 1"));
    }

    THEN("a ListDirectory without a directory is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(1));
      ListDirectory out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }

  GIVEN("a ListRootsResult")
  {
    ListRootsResult result;
    result.roots = {"/data", "/scratch/user", "/mnt/share"};

    THEN("roots keep their order")
    {
      ListRootsResult out;
      REQUIRE(roundTripNode(result, out));
      REQUIRE(out.roots == result.roots);
    }

    THEN("an empty result reads back empty")
    {
      ListRootsResult out;
      out.roots = {"/stale"};
      REQUIRE(roundTripNode(ListRootsResult{}, out));
      REQUIRE(out.roots.empty());
    }
  }

  GIVEN("a ListDirectoryResult with mixed entry kinds")
  {
    ListDirectoryResult result;
    result.entries.push_back({"zeta.vsr", EntryKind::File, 4096, 1700000000});
    result.entries.push_back({"alpha", EntryKind::Directory, 0, 1600000000});
    result.entries.push_back({"myProject", EntryKind::ProjectDirectory, 0, -1});
    result.entries.push_back(
        {"big.raw", EntryKind::File, uint64_t(1) << 40, 0});

    THEN("entry order, kinds, sizes and mtimes survive")
    {
      ListDirectoryResult out;
      REQUIRE(roundTripNode(result, out));
      REQUIRE(out.entries.size() == 4);
      REQUIRE(out.entries[0].name == "zeta.vsr");
      REQUIRE(out.entries[0].kind == EntryKind::File);
      REQUIRE(out.entries[0].size == 4096);
      REQUIRE(out.entries[0].mtimeSeconds == 1700000000);
      REQUIRE(out.entries[1].name == "alpha");
      REQUIRE(out.entries[1].kind == EntryKind::Directory);
      REQUIRE(out.entries[2].name == "myProject");
      REQUIRE(out.entries[2].kind == EntryKind::ProjectDirectory);
      REQUIRE(out.entries[2].mtimeSeconds == -1);
      REQUIRE(out.entries[3].size == (uint64_t(1) << 40));
    }

    THEN("kinds travel as enumerator names")
    {
      vsr::core::DataTree tree;
      toNode(result, tree.root());
      const auto *entries = tree.root().child("entries");
      REQUIRE(entries);
      REQUIRE(readChildOr(*entries->child("2"), "kind", std::string())
          == "ProjectDirectory");
      REQUIRE(entryKindFromString("File") == EntryKind::File);
      REQUIRE_FALSE(entryKindFromString("Symlink"));
    }

    THEN("an entry with an unknown kind rejects the whole list")
    {
      vsr::core::DataTree tree;
      toNode(result, tree.root());
      tree.root()["entries"]["1"]["kind"] = std::string("Symlink");
      ListDirectoryResult out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("an empty listing reads back empty")
    {
      ListDirectoryResult out;
      REQUIRE(roundTripNode(ListDirectoryResult{}, out));
      REQUIRE(out.entries.empty());
    }
  }
}

SCENARIO("Playback payloads", "[StudioProtocol]")
{
  GIVEN("SetPlaying, SetTime, TimeAdvanceWarning and RenderShot")
  {
    SetPlaying play;
    play.requestId = 21;
    play.shotId = "shot-a";
    play.playing = true;

    SetTime time;
    time.shotId = "shot-a";
    time.frame = 42;

    TimeAdvanceWarning warning;
    warning.shotId = "shot-a";
    warning.frame = 43;
    warning.message = "frame 43: file missing";

    RenderShot render;
    render.requestId = 22;
    render.shotId = "shot-b";

    THEN("each round-trips")
    {
      const auto p = decode<SetPlaying>(encode(play));
      REQUIRE(p);
      REQUIRE(p->requestId == 21);
      REQUIRE(p->shotId == "shot-a");
      REQUIRE(p->playing);

      const auto t = decode<SetTime>(encode(time));
      REQUIRE(t);
      REQUIRE(t->shotId == "shot-a");
      REQUIRE(t->frame == 42);

      const auto w = decode<TimeAdvanceWarning>(encode(warning));
      REQUIRE(w);
      REQUIRE(w->shotId == "shot-a");
      REQUIRE(w->frame == 43);
      REQUIRE(w->message == "frame 43: file missing");

      const auto r = decode<RenderShot>(encode(render));
      REQUIRE(r);
      REQUIRE(r->requestId == 22);
      REQUIRE(r->shotId == "shot-b");
    }

    THEN("playing false and frame 0 survive as values, not absences")
    {
      play.playing = false;
      time.frame = 0;
      const auto p = decode<SetPlaying>(encode(play));
      REQUIRE(p);
      REQUIRE_FALSE(p->playing);
      const auto t = decode<SetTime>(encode(time));
      REQUIRE(t);
      REQUIRE(t->frame == 0);
    }

    THEN("a warning without a message reads back empty")
    {
      warning.message.clear();
      const auto w = decode<TimeAdvanceWarning>(encode(warning));
      REQUIRE(w);
      REQUIRE(w->message.empty());
    }

    THEN("a SetTime with a mistyped frame is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "shotId", std::string("shot-a"));
      writeChild(tree.root(), "frame", 1.5f);
      SetTime out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }
}

SCENARIO("Optimistic scene edit payloads", "[StudioProtocol]")
{
  GIVEN("a SetObjectParameter")
  {
    SetObjectParameter edit;
    edit.object = makeRef(ANARI_GEOMETRY, 7);
    edit.name = "radius";

    THEN("a float value round-trips with its type")
    {
      edit.value = vsr::core::Any(0.25f);
      const auto out = decode<SetObjectParameter>(encode(edit));
      REQUIRE(out);
      REQUIRE(out->object.type == ANARI_GEOMETRY);
      REQUIRE(out->object.objectIndex == 7);
      REQUIRE(out->name == "radius");
      REQUIRE(out->value.is<float>());
      REQUIRE(out->value.getAs<float>() == 0.25f);
    }

    THEN("an int value round-trips with its type")
    {
      edit.value = vsr::core::Any(-3);
      const auto out = decode<SetObjectParameter>(encode(edit));
      REQUIRE(out);
      REQUIRE(out->value.is<int>());
      REQUIRE_FALSE(out->value.is<float>());
      REQUIRE(out->value.getAs<int>() == -3);
    }

    THEN("a float3 value round-trips with its type")
    {
      edit.value = vsr::core::Any(vsr::math::float3(1.f, 2.f, 3.f));
      const auto out = decode<SetObjectParameter>(encode(edit));
      REQUIRE(out);
      REQUIRE(out->value.is<vsr::math::float3>());
      REQUIRE(out->value.getAs<vsr::math::float3>()
          == vsr::math::float3(1.f, 2.f, 3.f));
    }

    THEN("a string value round-trips with its type")
    {
      edit.value = vsr::core::Any(std::string("scientific"));
      const auto out = decode<SetObjectParameter>(encode(edit));
      REQUIRE(out);
      REQUIRE(out->value.is<std::string>());
      REQUIRE(out->value.getString() == "scientific");
    }

    THEN("a bool and a mat4 value round-trip with their types")
    {
      edit.value = vsr::core::Any(true);
      auto out = decode<SetObjectParameter>(encode(edit));
      REQUIRE(out);
      REQUIRE(out->value.is<bool>());
      REQUIRE(out->value.getAs<bool>());

      auto m = vsr::math::IDENTITY_MAT4;
      m[3] = vsr::math::float4(1.f, 2.f, 3.f, 1.f);
      edit.value = vsr::core::Any(m);
      out = decode<SetObjectParameter>(encode(edit));
      REQUIRE(out);
      REQUIRE(out->value.is<vsr::math::mat4>());
      REQUIRE(out->value.getAs<vsr::math::mat4>() == m);
    }

    THEN("an empty value is rejected")
    {
      const auto out = decode<SetObjectParameter>(encode(edit));
      REQUIRE_FALSE(out);
    }

    THEN("an array value is rejected")
    {
      vsr::core::DataTree tree;
      writeChildNode(tree.root(), "object", edit.object);
      writeChild(tree.root(), "name", edit.name);
      tree.root()["value"].setValueAsArray(std::vector<float>{1.f, 2.f});
      SetObjectParameter out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a missing name is rejected")
    {
      vsr::core::DataTree tree;
      writeChildNode(tree.root(), "object", edit.object);
      tree.root()["value"] = 1.f;
      SetObjectParameter out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }

  GIVEN("a RemoveObjectParameter")
  {
    RemoveObjectParameter remove;
    remove.object = makeRef(ANARI_MATERIAL, 2);
    remove.name = "color";
    const auto out = decode<RemoveObjectParameter>(encode(remove));
    REQUIRE(out);
    REQUIRE(out->object.type == ANARI_MATERIAL);
    REQUIRE(out->object.objectIndex == 2);
    REQUIRE(out->name == "color");
  }

  GIVEN("a SetNodeTransform")
  {
    SetNodeTransform xf;
    xf.node.layerName = "lights";
    xf.node.nodeIndex = 4;
    xf.transform = vsr::math::IDENTITY_MAT4;
    xf.transform[3] = vsr::math::float4(10.f, 20.f, 30.f, 1.f);
    xf.transform[0][0] = 2.f;

    THEN("the node ref and matrix round-trip")
    {
      const auto out = decode<SetNodeTransform>(encode(xf));
      REQUIRE(out);
      REQUIRE(out->node.layerName == "lights");
      REQUIRE(out->node.nodeIndex == 4);
      REQUIRE(out->transform == xf.transform);
    }

    THEN("a missing transform is rejected")
    {
      vsr::core::DataTree tree;
      writeChildNode(tree.root(), "node", xf.node);
      SetNodeTransform out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }
}

SCENARIO("Viewport payloads", "[StudioProtocol]")
{
  GIVEN("a Pick request")
  {
    Pick pick;
    pick.requestId = 31;
    pick.x = 640;
    pick.y = -1;
    const auto out = decode<Pick>(encode(pick));
    REQUIRE(out);
    REQUIRE(out->requestId == 31);
    REQUIRE(out->x == 640);
    REQUIRE(out->y == -1);
  }

  GIVEN("a PickReply")
  {
    PickReply reply;
    reply.requestId = 31;
    reply.hit = true;
    reply.worldPosition = vsr::math::float3(1.5f, -2.f, 0.25f);

    THEN("with an object identity it round-trips")
    {
      reply.objectIdentity = makeRef(ANARI_VOLUME, 3);
      const auto out = decode<PickReply>(encode(reply));
      REQUIRE(out);
      REQUIRE(out->requestId == 31);
      REQUIRE(out->hit);
      REQUIRE(out->worldPosition == vsr::math::float3(1.5f, -2.f, 0.25f));
      REQUIRE(out->objectIdentity);
      REQUIRE(out->objectIdentity->type == ANARI_VOLUME);
      REQUIRE(out->objectIdentity->objectIndex == 3);
    }

    THEN("without an identity (background) it round-trips with it absent")
    {
      reply.hit = false;
      const auto msg = encode(reply);
      const auto out = decode<PickReply>(msg);
      REQUIRE(out);
      REQUIRE_FALSE(out->hit);
      REQUIRE_FALSE(out->objectIdentity);
      REQUIRE(out->worldPosition == vsr::math::float3(1.5f, -2.f, 0.25f));
    }

    THEN("a malformed objectIdentity child is rejected")
    {
      vsr::core::DataTree tree;
      toNode(reply, tree.root());
      tree.root()["objectIdentity"]["type"] = std::string("ANARI_BOGUS");
      PickReply out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }

  GIVEN("a SetOutline")
  {
    THEN("an empty outline round-trips as cleared")
    {
      SetOutline outline;
      const auto msg = encode(outline);
      REQUIRE(msg.header.type == uint8_t(StudioMessageType::SetOutline));
      const auto out = decode<SetOutline>(msg);
      REQUIRE(out);
      REQUIRE_FALSE(out->objectIdentity);
    }

    THEN("a set outline round-trips with its object identity")
    {
      SetOutline outline;
      outline.objectIdentity = makeRef(ANARI_SURFACE, 9);
      const auto out = decode<SetOutline>(encode(outline));
      REQUIRE(out);
      REQUIRE(out->objectIdentity);
      REQUIRE(out->objectIdentity->type == ANARI_SURFACE);
      REQUIRE(out->objectIdentity->objectIndex == 9);
    }
  }

  GIVEN("ViewportSettings")
  {
    ViewportSettings settings;
    settings.highlightSelection = false;
    settings.outlinePrimitives = true;
    settings.showWorldBounds = true;
    settings.worldBoundsColor = vsr::math::float4(1.f, 0.5f, 0.f, 1.f);
    settings.worldBoundsWidth = 3;
    settings.visualizeAOV = vsr::rendering::AOVType::OBJECT_ID;
    settings.depthVisualMinimum = 0.1f;
    settings.depthVisualMaximum = 100.f;
    settings.edgeInvert = true;

    THEN("every toggle round-trips")
    {
      const auto out = decode<ViewportSettings>(encode(settings));
      REQUIRE(out);
      REQUIRE_FALSE(out->highlightSelection);
      REQUIRE(out->outlinePrimitives);
      REQUIRE(out->showWorldBounds);
      REQUIRE(out->worldBoundsColor == vsr::math::float4(1.f, 0.5f, 0.f, 1.f));
      REQUIRE(out->worldBoundsWidth == 3);
      REQUIRE(out->visualizeAOV == vsr::rendering::AOVType::OBJECT_ID);
      REQUIRE(out->depthVisualMinimum == 0.1f);
      REQUIRE(out->depthVisualMaximum == 100.f);
      REQUIRE(out->edgeInvert);
    }

    THEN("the AOV mode travels as its enumerator name")
    {
      vsr::core::DataTree tree;
      toNode(settings, tree.root());
      REQUIRE(readChildOr(tree.root(), "visualizeAOV", std::string())
          == "OBJECT_ID");
      REQUIRE(aovTypeFromString("PRIMITIVE_ID")
          == vsr::rendering::AOVType::PRIMITIVE_ID);
      REQUIRE_FALSE(aovTypeFromString("object ID"));
    }

    THEN("defaults round-trip and an empty tree yields the defaults")
    {
      const auto out = decode<ViewportSettings>(encode(ViewportSettings{}));
      REQUIRE(out);
      REQUIRE(out->highlightSelection);
      REQUIRE_FALSE(out->outlinePrimitives);
      REQUIRE(out->visualizeAOV == vsr::rendering::AOVType::NONE);

      vsr::core::DataTree tree;
      ViewportSettings partial = settings;
      REQUIRE(fromNode(tree.root(), partial));
      REQUIRE(partial.highlightSelection);
      REQUIRE(partial.worldBoundsWidth == 1);
      REQUIRE(partial.visualizeAOV == vsr::rendering::AOVType::NONE);
    }

    THEN("a present but mistyped or unknown field is rejected")
    {
      vsr::core::DataTree tree;
      tree.root()["edgeInvert"] = std::string("yes");
      ViewportSettings out;
      REQUIRE_FALSE(fromNode(tree.root(), out));

      vsr::core::DataTree aov;
      aov.root()["visualizeAOV"] = std::string("HEATMAP");
      REQUIRE_FALSE(fromNode(aov.root(), out));
    }
  }

  GIVEN("a RequestArrayHistogram and its result")
  {
    RequestArrayHistogram request;
    request.requestId = 41;
    request.array = makeRef(ANARI_ARRAY1D, 15);
    request.binCount = 64;

    THEN("the request round-trips")
    {
      const auto out = decode<RequestArrayHistogram>(encode(request));
      REQUIRE(out);
      REQUIRE(out->requestId == 41);
      REQUIRE(out->array.type == ANARI_ARRAY1D);
      REQUIRE(out->array.objectIndex == 15);
      REQUIRE(out->binCount == 64);
    }

    THEN("the bins vector and range round-trip")
    {
      ArrayHistogramResult result;
      result.bins = {0, 5, 17, 1000000000000ull, 3, 0};
      result.minValue = -1.5f;
      result.maxValue = 42.f;
      result.nonFinite = 9;
      ArrayHistogramResult out;
      REQUIRE(roundTripNode(result, out));
      REQUIRE(out.bins == result.bins);
      REQUIRE(out.minValue == -1.5f);
      REQUIRE(out.maxValue == 42.f);
      REQUIRE(out.nonFinite == 9);
    }

    THEN("an absent nonFinite count reads as zero")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "minValue", 0.f);
      writeChild(tree.root(), "maxValue", 1.f);
      ArrayHistogramResult out;
      out.nonFinite = 5;
      REQUIRE(fromNode(tree.root(), out));
      REQUIRE(out.nonFinite == 0);
    }

    THEN("an empty bins vector reads back empty")
    {
      ArrayHistogramResult result;
      result.minValue = 0.f;
      result.maxValue = 1.f;
      ArrayHistogramResult out;
      out.bins = {9};
      REQUIRE(roundTripNode(result, out));
      REQUIRE(out.bins.empty());
    }

    THEN("bins of the wrong element type are rejected")
    {
      vsr::core::DataTree tree;
      tree.root()["bins"].setValueAsArray(std::vector<float>{1.f, 2.f});
      writeChild(tree.root(), "minValue", 0.f);
      writeChild(tree.root(), "maxValue", 1.f);
      ArrayHistogramResult out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }
}
