// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr_scivis_studio_protocol
#include "FrameMessages.h"
#include "PayloadCommon.h"
#include "SessionMessages.h"
#include "StudioCodec.h"
#include "StudioProtocol.h"
// std
#include <array>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace vsr::scivis_studio::protocol;
using vsr::scivis_studio::SceneNodeRef;
using vsr::scivis_studio::SceneObjectRef;

namespace {

// clang-format off
constexpr std::array ALL_MESSAGE_TYPES = {
  StudioMessageType::Hello, StudioMessageType::Error, StudioMessageType::Ping,
  StudioMessageType::Pong, StudioMessageType::Disconnect,
  StudioMessageType::Shutdown, StudioMessageType::BootstrapBegin,
  StudioMessageType::BootstrapEnd,
  StudioMessageType::NewProject, StudioMessageType::OpenProject,
  StudioMessageType::SaveProject, StudioMessageType::ImportStaticDataset,
  StudioMessageType::ImportFileAnimationDataset,
  StudioMessageType::DeclareFileAnimationDataset,
  StudioMessageType::ReimportDataset, StudioMessageType::RenameDataset,
  StudioMessageType::RemoveDataset, StudioMessageType::LoadDataset,
  StudioMessageType::UnloadDataset,
  StudioMessageType::RefreshDatasetAvailability,
  StudioMessageType::SaveDatasetArchive, StudioMessageType::LoadDatasetArchive,
  StudioMessageType::DiscoverDatasetCandidates,
  StudioMessageType::IncorporateDatasetCandidate,
  StudioMessageType::CreateShot, StudioMessageType::RemoveShot,
  StudioMessageType::UpdateShot, StudioMessageType::SetActiveShot,
  StudioMessageType::CreateLightRig, StudioMessageType::CloneLightRig,
  StudioMessageType::RemoveLightRig, StudioMessageType::RenameLightRig,
  StudioMessageType::AddLightToRig, StudioMessageType::RemoveLightFromRig,
  StudioMessageType::CreateCameraRig, StudioMessageType::RemoveCameraRig,
  StudioMessageType::RenameCameraRig, StudioMessageType::SaveCameraRigArchive,
  StudioMessageType::LoadCameraRigArchive,
  StudioMessageType::SaveLightRigArchive,
  StudioMessageType::LoadLightRigArchive,
  StudioMessageType::CreateColorMap, StudioMessageType::RenameColorMap,
  StudioMessageType::RemoveColorMap,
  StudioMessageType::ListRoots, StudioMessageType::ListDirectory,
  StudioMessageType::SetPlaying, StudioMessageType::RequestArrayHistogram,
  StudioMessageType::RenderShot, StudioMessageType::CancelTask,
  StudioMessageType::Pick, StudioMessageType::ImportSubtreeDataset,
  StudioMessageType::ProjectOpReply, StudioMessageType::ProjectSnapshot,
  StudioMessageType::TaskProgress, StudioMessageType::TaskCompleted,
  StudioMessageType::TaskFailed, StudioMessageType::TimeAdvanceWarning,
  StudioMessageType::PickReply, StudioMessageType::UIState,
  StudioMessageType::TransferScene, StudioMessageType::TransferLayer,
  StudioMessageType::ObjectAdded, StudioMessageType::ObjectRemoved,
  StudioMessageType::SetObjectParameter,
  StudioMessageType::RemoveObjectParameter,
  StudioMessageType::SetNodeTransform, StudioMessageType::SetTime,
  StudioMessageType::SetOutline, StudioMessageType::ViewportSettings,
  StudioMessageType::SetFrameConfig, StudioMessageType::FrameConfig,
  StudioMessageType::SetEncodings, StudioMessageType::StartRendering,
  StudioMessageType::StopRendering, StudioMessageType::Frame,
};
// clang-format on

} // namespace

SCENARIO("StudioMessageType enum", "[StudioProtocol]")
{
  GIVEN("every enumerator")
  {
    THEN("each is recognised, named, unique and never 0 or 255")
    {
      std::set<uint8_t> seen;
      for (auto t : ALL_MESSAGE_TYPES) {
        const auto v = uint8_t(t);
        REQUIRE(v != 0);
        REQUIRE(v != 255);
        REQUIRE(v != vsr::network::MESSAGE_TYPE_INVALID);
        REQUIRE(isStudioMessageType(v));
        REQUIRE(std::string(toString(t)) != "Unknown");
        REQUIRE(seen.insert(v).second);
      }
    }

    THEN("isStudioMessageType() accepts exactly the enumerators")
    {
      size_t count = 0;
      for (int v = 0; v < 256; ++v)
        count += isStudioMessageType(uint8_t(v)) ? 1 : 0;
      REQUIRE(count == ALL_MESSAGE_TYPES.size());
      REQUIRE_FALSE(isStudioMessageType(0));
      REQUIRE_FALSE(isStudioMessageType(255));
    }

    THEN("toString() of an undefined value is 'Unknown'")
    {
      REQUIRE(std::string(toString(StudioMessageType(0))) == "Unknown");
      REQUIRE(std::string(toString(StudioMessageType(255))) == "Unknown");
    }

    THEN("every table row names its own enumerator and round-trips")
    {
      // The hand-written ALL_MESSAGE_TYPES above and the table in
      // StudioProtocol.h must agree, or a row was lost on one side.
      REQUIRE(MESSAGE_TYPE_TABLE.size() == ALL_MESSAGE_TYPES.size());
#define X(name, value, dir)                                                    \
  REQUIRE(std::string(toString(StudioMessageType::name)) == #name);            \
  REQUIRE(uint8_t(StudioMessageType::name) == value);                          \
  REQUIRE(isStudioMessageType(value));
      STUDIO_MESSAGE_TYPES(X)
#undef X
    }

    THEN("isServerToClient() marks exactly the server-only types")
    {
      constexpr std::array SERVER_TO_CLIENT = {
          StudioMessageType::BootstrapBegin,
          StudioMessageType::BootstrapEnd,
          StudioMessageType::ProjectOpReply,
          StudioMessageType::ProjectSnapshot,
          StudioMessageType::TaskProgress,
          StudioMessageType::TaskCompleted,
          StudioMessageType::TaskFailed,
          StudioMessageType::TimeAdvanceWarning,
          StudioMessageType::PickReply,
          StudioMessageType::UIState,
          StudioMessageType::TransferScene,
          StudioMessageType::TransferLayer,
          StudioMessageType::ObjectAdded,
          StudioMessageType::ObjectRemoved,
          StudioMessageType::FrameConfig,
          StudioMessageType::Frame};
      const std::set<StudioMessageType> serverOnly(
          SERVER_TO_CLIENT.begin(), SERVER_TO_CLIENT.end());
      for (auto t : ALL_MESSAGE_TYPES)
        REQUIRE(isServerToClient(t) == (serverOnly.count(t) == 1));
      // The session messages go both ways; the optimistic edits and the
      // rendering controls are client-to-server.
      static_assert(!isServerToClient(StudioMessageType::Hello));
      static_assert(!isServerToClient(StudioMessageType::Error));
      static_assert(!isServerToClient(StudioMessageType::SetObjectParameter));
      static_assert(!isServerToClient(StudioMessageType::SetFrameConfig));
      static_assert(isServerToClient(StudioMessageType::Frame));
      REQUIRE_FALSE(isServerToClient(StudioMessageType(0)));
      REQUIRE_FALSE(isServerToClient(StudioMessageType(255)));
    }
  }
}

SCENARIO("parsePort() reads --port values", "[StudioProtocol]")
{
  GIVEN("a port variable")
  {
    uint16_t port = DEFAULT_PORT;

    THEN("decimal integers in 1..65535 are accepted")
    {
      REQUIRE(parsePort("1", port));
      REQUIRE(port == 1);
      REQUIRE(parsePort("65535", port));
      REQUIRE(port == 65535);
      REQUIRE(parsePort(std::to_string(DEFAULT_PORT), port));
      REQUIRE(port == DEFAULT_PORT);
    }

    THEN("anything else is refused and leaves the port alone")
    {
      for (const char *bad :
          {"0", "65536", "-1", "abc", "12a", "", " 12", "12 ", "+12", "0x10"}) {
        port = DEFAULT_PORT;
        REQUIRE_FALSE(parsePort(bad, port));
        REQUIRE(port == DEFAULT_PORT);
      }
    }
  }
}

SCENARIO("StudioCodec encode/decode", "[StudioProtocol]")
{
  GIVEN("a Hello payload")
  {
    Hello hello;
    hello.version = 7;
    hello.buildInfo = "vela-test";
    const auto msg = encode(hello);

    THEN("the header carries the type and payload length")
    {
      REQUIRE(msg.header.type == uint8_t(StudioMessageType::Hello));
      REQUIRE(msg.header.payload_length == msg.payload.size());
      REQUIRE(messageType(msg) == StudioMessageType::Hello);
    }

    THEN("it decodes back to an equal payload")
    {
      const auto out = decode<Hello>(msg);
      REQUIRE(out);
      REQUIRE(out->version == 7);
      REQUIRE(out->buildInfo == "vela-test");
    }

    THEN("decoding as a different type returns empty")
    {
      REQUIRE_FALSE(decode<Ping>(msg));
      REQUIRE_FALSE(decode<Error>(msg));
    }

    THEN("a missing required child is rejected")
    {
      vsr::core::DataTree tree;
      tree.root()["buildInfo"] = std::string("no version here");
      Hello out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a mistyped required child is rejected")
    {
      vsr::core::DataTree tree;
      tree.root()["version"] = std::string("one");
      Hello out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a mistyped optional child is rejected, not defaulted")
    {
      vsr::core::DataTree tree;
      tree.root()["version"] = 7;
      tree.root()["buildInfo"] = 7;
      Hello out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }

  GIVEN("an Error payload")
  {
    Error err;
    err.message = "something broke";
    const auto out = decode<Error>(encode(err));
    REQUIRE(out);
    REQUIRE(out->message == "something broke");

    THEN("an empty message round-trips too")
    {
      const auto empty = decode<Error>(encode(Error{}));
      REQUIRE(empty);
      REQUIRE(empty->message.empty());
    }
  }

  GIVEN("a Disconnect payload")
  {
    // The server's farewell names why it closes; a client's courtesy
    // Disconnect carries no reason and reads back as "".
    Disconnect farewell;
    farewell.reason = "replaced by another client";
    const auto out = decode<Disconnect>(encode(farewell));
    REQUIRE(out);
    REQUIRE(out->reason == "replaced by another client");

    THEN("a bare Disconnect round-trips with an empty reason")
    {
      const auto bare = decode<Disconnect>(encode(Disconnect{}));
      REQUIRE(bare);
      REQUIRE(bare->reason.empty());
    }

    THEN("a header-only Disconnect with no payload bytes is rejected")
    {
      const auto msg =
          vsr::network::makeMessage(uint8_t(StudioMessageType::Disconnect));
      REQUIRE_FALSE(decode<Disconnect>(msg));
    }
  }

  GIVEN("empty payloads")
  {
    THEN("Ping encodes and decodes")
    {
      const auto msg = encode(Ping{});
      REQUIRE(msg.header.type == uint8_t(StudioMessageType::Ping));
      REQUIRE(decode<Ping>(msg));
      REQUIRE_FALSE(decode<Hello>(msg));
      REQUIRE_FALSE(decode<Pong>(msg));
    }

    THEN("every session empty payload decodes")
    {
      REQUIRE(decode<Pong>(encode(Pong{})));
      REQUIRE(decode<Shutdown>(encode(Shutdown{})));
      REQUIRE(decode<BootstrapBegin>(encode(BootstrapBegin{})));
      REQUIRE(decode<BootstrapEnd>(encode(BootstrapEnd{})));
    }

    THEN("a header-only message with no bytes is not an empty payload")
    {
      const auto msg =
          vsr::network::makeMessage(uint8_t(StudioMessageType::Ping));
      REQUIRE_FALSE(decode<Ping>(msg));
    }
  }

  GIVEN("malformed messages")
  {
    THEN("a message outside the set has no messageType()")
    {
      auto msg = vsr::network::makeMessage(0);
      REQUIRE_FALSE(messageType(msg));
      msg.header.type = 255;
      REQUIRE_FALSE(messageType(msg));
      msg.header.type = 19; // gap between groups
      REQUIRE_FALSE(messageType(msg));
    }

    THEN("a truncated payload decodes to empty rather than throwing")
    {
      Hello hello;
      hello.buildInfo = "long enough to truncate meaningfully";
      auto msg = encode(hello);
      msg.payload.resize(msg.payload.size() / 2);
      msg.header.payload_length = uint32_t(msg.payload.size());
      REQUIRE_NOTHROW(decode<Hello>(msg));
      REQUIRE_FALSE(decode<Hello>(msg));
    }

    THEN("a garbage payload decodes to empty rather than throwing")
    {
      auto msg = vsr::network::makeMessage(uint8_t(StudioMessageType::Hello));
      for (int i = 0; i < 64; ++i)
        msg.payload.push_back(std::byte(0xA5 ^ (i * 37)));
      msg.header.payload_length = uint32_t(msg.payload.size());
      REQUIRE_NOTHROW(decode<Hello>(msg));
      REQUIRE_FALSE(decode<Hello>(msg));
    }
  }
}

SCENARIO("PayloadCommon helpers", "[StudioProtocol]")
{
  GIVEN("scalar read helpers")
  {
    vsr::core::DataTree tree;
    auto &root = tree.root();
    writeChild(root, "count", 3);
    writeChild(root, "big", size_t(1) << 40);
    writeChild(root, "flag", true);
    writeChild(root, "name", std::string("n"));

    THEN("readChild() returns present, well-typed values")
    {
      int count = 0;
      size_t big = 0;
      bool flag = false;
      std::string name;
      REQUIRE(readChild(root, "count", count));
      REQUIRE(readChild(root, "big", big));
      REQUIRE(readChild(root, "flag", flag));
      REQUIRE(readChild(root, "name", name));
      REQUIRE(count == 3);
      REQUIRE(big == (size_t(1) << 40));
      REQUIRE(flag);
      REQUIRE(name == "n");
    }

    THEN("readChild() is false for a missing child and does not create it")
    {
      int value = 42;
      REQUIRE_FALSE(readChild(root, "missing", value));
      REQUIRE(value == 42);
      REQUIRE_FALSE(hasChild(root, "missing"));
    }

    THEN("readChild() is false for a mistyped child")
    {
      std::string text;
      REQUIRE_FALSE(readChild(root, "count", text));
      float f = 0.f;
      REQUIRE_FALSE(readChild(root, "count", f));
    }
  }

  GIVEN("string lists")
  {
    vsr::core::DataTree tree;
    auto &root = tree.root();
    const std::vector<std::string> items = {"b", "a", "c"};
    writeStringList(root, "items", items);
    writeStringList(root, "none", {});

    THEN("order survives and an absent list reads as empty")
    {
      std::vector<std::string> out;
      REQUIRE(readStringList(root, "items", out));
      REQUIRE(out == items);
      REQUIRE(readStringList(root, "none", out));
      REQUIRE(out.empty());
      REQUIRE_FALSE(hasChild(root, "none"));
    }

    THEN("order survives a serialization round trip")
    {
      vsr::network::MessagePayload bytes;
      tree.write(bytes);
      vsr::core::DataTree copy;
      REQUIRE(copy.read(bytes));
      std::vector<std::string> out;
      REQUIRE(readStringList(copy.root(), "items", out));
      REQUIRE(out == items);
    }
  }

  GIVEN("paths")
  {
    vsr::core::DataTree tree;
    writePath(tree.root(), "dir", std::filesystem::path("/data/run 1/a.vsr"));
    std::filesystem::path out;
    REQUIRE(readPath(tree.root(), "dir", out));
    REQUIRE(out == std::filesystem::path("/data/run 1/a.vsr"));
    REQUIRE_FALSE(readPath(tree.root(), "missing", out));
  }

  GIVEN("enum strings")
  {
    enum class Mode
    {
      A,
      B
    };
    const auto parse = [](const std::string &s) -> std::optional<Mode> {
      if (s == "A")
        return Mode::A;
      if (s == "B")
        return Mode::B;
      return {};
    };
    vsr::core::DataTree tree;
    tree.root()["mode"] = std::string("B");
    tree.root()["bad"] = std::string("Z");
    Mode mode = Mode::A;
    REQUIRE(readEnumChild(tree.root(), "mode", mode, parse));
    REQUIRE(mode == Mode::B);
    REQUIRE_FALSE(readEnumChild(tree.root(), "bad", mode, parse));
    REQUIRE_FALSE(readEnumChild(tree.root(), "missing", mode, parse));
    REQUIRE(mode == Mode::B);
  }
}

SCENARIO("Scene identity payloads", "[StudioProtocol]")
{
  GIVEN("a SceneObjectRef")
  {
    SceneObjectRef ref;
    ref.type = ANARI_SPATIAL_FIELD;
    ref.objectIndex = 12;

    THEN("it travels as one object-reference leaf")
    {
      vsr::core::DataTree tree;
      toNode(ref, tree.root()["object"]);
      const auto *leaf = tree.root().child("object");
      REQUIRE(leaf->holdsObjectIdx());
      REQUIRE(leaf->numChildren() == 0);

      vsr::network::MessagePayload bytes;
      tree.write(bytes);
      vsr::core::DataTree copy;
      REQUIRE(copy.read(bytes));
      SceneObjectRef out;
      REQUIRE(readChildNode(copy.root(), "object", out));
      REQUIRE(out.type == ANARI_SPATIAL_FIELD);
      REQUIRE(out.objectIndex == 12);
    }

    THEN("an unset ref is an empty leaf and reads back unset")
    {
      vsr::core::DataTree tree;
      toNode(SceneObjectRef{}, tree.root()["camera"]);
      REQUIRE(tree.root().child("camera")->empty());

      vsr::network::MessagePayload bytes;
      tree.write(bytes);
      vsr::core::DataTree copy;
      REQUIRE(copy.read(bytes));
      SceneObjectRef out;
      out.type = ANARI_CAMERA;
      out.objectIndex = 3;
      REQUIRE(readChildNode(copy.root(), "camera", out));
      REQUIRE(out.type == ANARI_UNKNOWN);
      REQUIRE(out.objectIndex == VSR_INVALID_INDEX);
    }

    THEN("a leaf holding anything else, or a subtree, is rejected")
    {
      vsr::core::DataTree tree;
      tree.root()["object"] = std::string("ANARI_CAMERA");
      SceneObjectRef out;
      REQUIRE_FALSE(readChildNode(tree.root(), "object", out));

      tree.root()["object"] = uint64_t(12);
      REQUIRE_FALSE(readChildNode(tree.root(), "object", out));

      // The pre-v5 shape: a {type, objectIndex} pair of children.
      vsr::core::DataTree pair;
      pair.root()["object"]["type"] = std::string("ANARI_CAMERA");
      writeChild(pair.root()["object"], "objectIndex", size_t(1));
      REQUIRE_FALSE(readChildNode(pair.root(), "object", out));
    }

    THEN("writeChildNode()/readChildNode() nest it under a named child")
    {
      vsr::core::DataTree tree;
      writeChildNode(tree.root(), "target", ref);
      SceneObjectRef out;
      REQUIRE(readChildNode(tree.root(), "target", out));
      REQUIRE(out.type == ref.type);
      REQUIRE(out.objectIndex == ref.objectIndex);
      REQUIRE_FALSE(readChildNode(tree.root(), "other", out));
    }
  }

  GIVEN("a SceneNodeRef")
  {
    SceneNodeRef ref;
    ref.layerName = "lights";
    ref.nodeIndex = 5;

    THEN("it round-trips through a serialized tree")
    {
      vsr::core::DataTree tree;
      toNode(ref, tree.root());
      vsr::network::MessagePayload bytes;
      tree.write(bytes);
      vsr::core::DataTree copy;
      REQUIRE(copy.read(bytes));
      SceneNodeRef out;
      REQUIRE(fromNode(copy.root(), out));
      REQUIRE(out.layerName == "lights");
      REQUIRE(out.nodeIndex == 5);
    }

    THEN("a missing layerName is rejected")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "nodeIndex", size_t(5));
      SceneNodeRef out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }
}

SCENARIO("Opaque subtrees", "[StudioProtocol]")
{
  GIVEN("a nested subtree held through a SubtreePtr")
  {
    auto subtree = makeSubtree();
    auto &r = subtree->root();
    r["windows"]["viewport"]["width"] = 640;
    r["windows"]["viewport"]["open"] = true;
    r["settings"]["theme"] = std::string("dark");

    THEN("it round-trips as a named child of a payload tree")
    {
      vsr::core::DataTree tree;
      writeChild(tree.root(), "requestId", uint64_t(9));
      writeSubtree(tree.root(), "uiState", subtree);
      writeSubtree(tree.root(), "absent", SubtreePtr{});
      REQUIRE_FALSE(hasChild(tree.root(), "absent"));

      vsr::network::MessagePayload bytes;
      tree.write(bytes);
      vsr::core::DataTree copy;
      REQUIRE(copy.read(bytes));

      uint64_t requestId = 0;
      REQUIRE(readChild(copy.root(), "requestId", requestId));
      REQUIRE(requestId == 9);

      auto out = readSubtree(copy.root(), "uiState");
      REQUIRE(out);
      const auto &o = out->root();
      REQUIRE(o.name() == "<root>");
      REQUIRE(o.numChildren() == 2);
      const auto *viewport = o.child("windows")->child("viewport");
      REQUIRE(viewport->child("width")->getValueOr(0) == 640);
      REQUIRE(viewport->child("open")->getValueOr(false));
      REQUIRE(o.child("settings")->child("theme")->getValueOr(std::string())
          == "dark");
      REQUIRE_FALSE(readSubtree(copy.root(), "absent"));
    }

    THEN("the copy is independent of the source")
    {
      vsr::core::DataTree tree;
      writeSubtree(tree.root(), "uiState", subtree);
      r["settings"]["theme"] = std::string("light");
      auto out = readSubtree(tree.root(), "uiState");
      REQUIRE(out);
      REQUIRE(out->root()
                  .child("settings")
                  ->child("theme")
                  ->getValueOr(std::string())
          == "dark");
    }
  }
}

namespace {

// A payload exercising every visitor verb once.
struct Probe
{
  uint64_t id{0};
  std::string label{"none"};
  std::optional<std::filesystem::path> where;
  FrameEncoding encoding{FrameEncoding::Raw};
  SceneNodeRef target;
  std::optional<SceneObjectRef> object;
  std::vector<std::string> tags;
  std::vector<SceneNodeRef> nodes;
  SubtreePtr extra;
};

template <typename V>
void fields(V &v, Probe &p)
{
  v.required("id", p.id);
  v.optional("label", p.label);
  v.optional("where", p.where);
  v.requiredEnum("encoding", p.encoding, toString, frameEncodingFromString);
  v.child("target", p.target);
  v.optionalChild("object", p.object);
  v.list("tags", p.tags);
  v.list("nodes", p.nodes);
  v.subtree("extra", p.extra);
}

SceneNodeRef node(const char *layer, size_t index)
{
  SceneNodeRef ref;
  ref.layerName = layer;
  ref.nodeIndex = index;
  return ref;
}

// The smallest well-formed Probe tree: every required child, nothing else.
void writeRequired(vsr::core::DataNode &root)
{
  writeChild(root, "id", uint64_t(1));
  writeChild(root, "encoding", std::string("Raw"));
  toNode(node("lights", 2), root["target"]);
}

} // namespace

SCENARIO("A fields() description derives a payload's codec", "[StudioProtocol]")
{
  GIVEN("a Probe with every field set")
  {
    Probe in;
    in.id = 42;
    in.label = "probe";
    in.where = std::filesystem::path("/data/run");
    in.encoding = FrameEncoding::TurboJpeg;
    in.target = node("geometry", 3);
    SceneObjectRef object;
    object.type = ANARI_MATERIAL;
    object.objectIndex = 5;
    in.object = object;
    in.tags = {"b", "a"};
    in.nodes = {node("l", 1), node("m", 2)};
    in.extra = makeSubtree();
    in.extra->root()["k"] = 7;

    vsr::core::DataTree tree;
    toNode(in, tree.root());

    THEN("each field travels under the name fields() spells")
    {
      const auto &root = tree.root();
      REQUIRE(root.child("id")->getValueOr(uint64_t(0)) == 42);
      REQUIRE(root.child("label")->getValueOr(std::string()) == "probe");
      REQUIRE(root.child("where")->getValueOr(std::string()) == "/data/run");
      REQUIRE(root.child("encoding")->getValueOr(std::string()) == "TurboJpeg");
      REQUIRE(root.child("target")->child("nodeIndex")->getValueOr(uint64_t(0))
          == 3);
      REQUIRE(root.child("object")->holdsObjectIdx());
      REQUIRE(root.child("tags")->child("1")->getValueOr(std::string()) == "a");
      REQUIRE(root.child("nodes")
                  ->child("1")
                  ->child("layerName")
                  ->getValueOr(std::string())
          == "m");
      REQUIRE(root.child("extra")->child("k")->getValueOr(0) == 7);
    }

    THEN("it reads back equal")
    {
      Probe out;
      REQUIRE(fromNode(tree.root(), out));
      REQUIRE(out.id == 42);
      REQUIRE(out.label == "probe");
      REQUIRE(out.where == std::filesystem::path("/data/run"));
      REQUIRE(out.encoding == FrameEncoding::TurboJpeg);
      REQUIRE(out.target.layerName == "geometry");
      REQUIRE(out.target.nodeIndex == 3);
      REQUIRE(out.object);
      REQUIRE(out.object->type == ANARI_MATERIAL);
      REQUIRE(out.object->objectIndex == 5);
      REQUIRE(out.tags == std::vector<std::string>{"b", "a"});
      REQUIRE(out.nodes.size() == 2);
      REQUIRE(out.nodes[1].layerName == "m");
      REQUIRE(out.extra);
      REQUIRE(out.extra->root().child("k")->getValueOr(0) == 7);
    }
  }

  GIVEN("a Probe with nothing optional set")
  {
    Probe in;
    in.id = 1;
    in.encoding = FrameEncoding::Raw;
    in.target = node("lights", 2);
    vsr::core::DataTree tree;
    toNode(in, tree.root());

    THEN("disengaged optionals, empty lists and a null subtree write nothing")
    {
      REQUIRE(tree.root().numChildren() == 4);
      REQUIRE_FALSE(hasChild(tree.root(), "where"));
      REQUIRE_FALSE(hasChild(tree.root(), "object"));
      REQUIRE_FALSE(hasChild(tree.root(), "tags"));
      REQUIRE_FALSE(hasChild(tree.root(), "nodes"));
      REQUIRE_FALSE(hasChild(tree.root(), "extra"));
    }
  }

  GIVEN("a tree holding only the required children")
  {
    vsr::core::DataTree tree;
    writeRequired(tree.root());

    THEN("absent optional children read as the struct's defaults")
    {
      Probe out;
      out.label = "stale";
      out.tags = {"stale"};
      REQUIRE(fromNode(tree.root(), out));
      REQUIRE(out.id == 1);
      REQUIRE(out.label == "none");
      REQUIRE_FALSE(out.where);
      REQUIRE_FALSE(out.object);
      REQUIRE(out.tags.empty());
      REQUIRE(out.nodes.empty());
      REQUIRE_FALSE(out.extra);
    }

    THEN("a present but mistyped optional child is rejected")
    {
      tree.root()["label"] = 7;
      Probe out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a present but mistyped std::optional child is rejected")
    {
      tree.root()["where"] = 7;
      Probe out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a present but malformed optional nested payload is rejected")
    {
      tree.root()["object"] = std::string("ANARI_MATERIAL");
      Probe out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a mistyped list item is rejected")
    {
      tree.root()["tags"]["0"] = 3;
      Probe out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a rejected payload leaves the output untouched")
    {
      tree.root()["label"] = 7;
      Probe out;
      out.id = 99;
      out.label = "kept";
      REQUIRE_FALSE(fromNode(tree.root(), out));
      REQUIRE(out.id == 99);
      REQUIRE(out.label == "kept");
    }
  }

  GIVEN("trees missing or mistyping a required child")
  {
    THEN("an absent required scalar is rejected")
    {
      vsr::core::DataTree tree;
      writeRequired(tree.root());
      tree.root().remove("id");
      Probe out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("a mistyped required scalar is rejected")
    {
      vsr::core::DataTree tree;
      writeRequired(tree.root());
      tree.root()["id"] = std::string("one");
      Probe out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("an unknown enum spelling is rejected")
    {
      vsr::core::DataTree tree;
      writeRequired(tree.root());
      tree.root()["encoding"] = std::string("Teapot");
      Probe out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }

    THEN("an absent required nested payload is rejected")
    {
      vsr::core::DataTree tree;
      writeRequired(tree.root());
      tree.root().remove("target");
      Probe out;
      REQUIRE_FALSE(fromNode(tree.root(), out));
    }
  }

  GIVEN("a Reader that has already failed")
  {
    vsr::core::DataTree tree;
    writeChild(tree.root(), "b", 2);
    Reader reader(tree.root());
    int a = 0;
    int b = 0;
    reader.required("a", a);
    REQUIRE_FALSE(reader.ok());

    THEN("later fields are skipped")
    {
      reader.required("b", b);
      REQUIRE_FALSE(reader.ok());
      REQUIRE(b == 0);
    }
  }
}
