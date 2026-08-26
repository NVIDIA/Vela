// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/DataPath.hpp"
#include "vsr/core/DataTree.hpp"
// std
#include <string>
#include <vector>

namespace {

std::vector<std::string> segmentTexts(const vsr::core::DataPath &path)
{
  std::vector<std::string> texts;
  for (auto segment : path)
    texts.emplace_back(segment.text());
  return texts;
}

} // namespace

SCENARIO("vsr::core::DataPath construction", "[DataPath]")
{
  GIVEN("A default constructed DataPath")
  {
    vsr::core::DataPath path;

    THEN("It is the root path")
    {
      REQUIRE(path.isRoot());
      REQUIRE(path.str() == "/");
    }

    THEN("It has no segments")
    {
      REQUIRE(path.numSegments() == 0);
      REQUIRE(path.begin() == path.end());
    }
  }

  GIVEN("A DataPath built from a string")
  {
    vsr::core::DataPath path("/objectDB/surface/name");

    THEN("It reports the string it was built from")
    {
      REQUIRE(path.str() == "/objectDB/surface/name");
      REQUIRE(!path.isRoot());
    }

    THEN("It iterates its segments in order")
    {
      REQUIRE(path.numSegments() == 3);
      REQUIRE(segmentTexts(path)
          == std::vector<std::string>{"objectDB", "surface", "name"});
    }
  }

  GIVEN("A path string missing its leading separator")
  {
    vsr::core::DataPath path("objectDB/surface");

    THEN("The leading separator is added")
    {
      REQUIRE(path.str() == "/objectDB/surface");
    }
  }

  GIVEN("A path string with a trailing separator")
  {
    vsr::core::DataPath path("/objectDB/surface/");

    THEN("The trailing separator is removed")
    {
      REQUIRE(path.str() == "/objectDB/surface");
      REQUIRE(path.numSegments() == 2);
    }
  }

  GIVEN("An empty path string")
  {
    vsr::core::DataPath path("");

    THEN("It is the root path")
    {
      REQUIRE(path.isRoot());
      REQUIRE(path.str() == "/");
    }
  }
}

SCENARIO("vsr::core::DataPath segments", "[DataPath]")
{
  GIVEN("A path with a named and an ordinal segment")
  {
    auto path = vsr::core::DataPath().child("objectDB").child(size_t(3));

    THEN("The path spells the ordinal in brackets")
    {
      REQUIRE(path.str() == "/objectDB/[3]");
    }

    THEN("The named segment reports its name")
    {
      auto segment = *path.begin();
      REQUIRE(!segment.isOrdinal());
      REQUIRE(segment.name() == "objectDB");
      REQUIRE(segment.ordinal() == VSR_INVALID_INDEX);
    }

    THEN("The ordinal segment reports its ordinal")
    {
      auto it = path.begin();
      ++it;
      auto segment = *it;
      REQUIRE(segment.isOrdinal());
      REQUIRE(segment.ordinal() == 3);
      REQUIRE(segment.name().empty());
    }
  }

  GIVEN("A path through a node legitimately named '3'")
  {
    auto path = vsr::core::DataPath().child("3");

    THEN("The segment is not confused with an ordinal")
    {
      REQUIRE(path.str() == "/3");
      auto segment = *path.begin();
      REQUIRE(!segment.isOrdinal());
      REQUIRE(segment.name() == "3");
    }
  }

  GIVEN("A named child whose name contains the path separator")
  {
    auto path = vsr::core::DataPath().child("a/b");

    THEN("The name is sanitized rather than adding a segment")
    {
      REQUIRE(path.str() == "/a_b");
      REQUIRE(path.numSegments() == 1);
    }
  }
}

SCENARIO("vsr::core::DataPath comparison", "[DataPath]")
{
  GIVEN("Two paths built the same way")
  {
    auto a = vsr::core::DataPath().child("x").child("y");
    vsr::core::DataPath b("/x/y");

    THEN("They compare equal")
    {
      REQUIRE(a == b);
      REQUIRE(!(a != b));
    }
  }

  GIVEN("A subtree path and paths inside and outside it")
  {
    vsr::core::DataPath subtree("/objectDB/surface");

    THEN("Paths at or below it are matched by the prefix test")
    {
      REQUIRE(subtree.isPrefixOf(subtree));
      REQUIRE(
          subtree.isPrefixOf(vsr::core::DataPath("/objectDB/surface/name")));
    }

    THEN("A sibling sharing a textual prefix is not matched")
    {
      REQUIRE(!subtree.isPrefixOf(vsr::core::DataPath("/objectDB/surfaceB")));
    }

    THEN("An unrelated path is not matched")
    {
      REQUIRE(!subtree.isPrefixOf(vsr::core::DataPath("/other")));
    }

    THEN("The root path is a prefix of everything")
    {
      REQUIRE(vsr::core::DataPath().isPrefixOf(subtree));
      REQUIRE(vsr::core::DataPath().isPrefixOf(vsr::core::DataPath()));
    }
  }
}

SCENARIO("vsr::core::DataPath addresses nodes in a DataTree", "[DataPath]")
{
  GIVEN("A tree with named and anonymous nodes")
  {
    vsr::core::DataTree tree;
    auto &root = tree.root();
    auto &named = root["objectDB"]["surface"];
    auto &first = named.append();
    auto &second = named.append();
    second["name"] = "second";

    THEN("The root's path is the root path")
    {
      REQUIRE(root.path().isRoot());
    }

    THEN("A named node's path is spelled with names")
    {
      REQUIRE(named.path().str() == "/objectDB/surface");
    }

    THEN("An anonymous node's path is spelled with its ordinal")
    {
      REQUIRE(first.path().str() == "/objectDB/surface/[0]");
      REQUIRE(second.path().str() == "/objectDB/surface/[1]");
    }

    THEN("A named child of an anonymous node mixes both spellings")
    {
      REQUIRE(second["name"].path().str() == "/objectDB/surface/[1]/name");
    }

    THEN("The same node always produces the same path")
    {
      REQUIRE(second.path() == second.path());
      REQUIRE(named.path() != second.path());
    }

    THEN("A path resolves back to the node it came from")
    {
      REQUIRE(tree.node(named.path()) == &named);
      REQUIRE(tree.node(first.path()) == &first);
      REQUIRE(tree.node(second["name"].path()) == &second["name"]);
      REQUIRE(tree.node(vsr::core::DataPath()) == &root);
    }

    THEN("A path that does not exist resolves to nothing")
    {
      REQUIRE(tree.node(vsr::core::DataPath("/objectDB/missing")) == nullptr);
      REQUIRE(
          tree.node(vsr::core::DataPath("/objectDB/surface/[7]")) == nullptr);
    }

    THEN("Resolving a missing path does not create nodes")
    {
      const auto childCount = root["objectDB"].numChildren();
      tree.node(vsr::core::DataPath("/objectDB/missing/deeper"));
      REQUIRE(root["objectDB"].numChildren() == childCount);
      REQUIRE(root["objectDB"].child("missing") == nullptr);
    }
  }

  GIVEN("An anonymous node copy-assigned onto a named one")
  {
    vsr::core::DataTree tree;
    auto &sequence = tree.root()["sequence"];
    sequence.append()["value"] = 1;

    auto &named = tree.root()["named"];
    named = *sequence.child(size_t(0));

    THEN("The destination keeps its own identity, not the source's")
    {
      REQUIRE(named.path().str() == "/named");
      REQUIRE(tree.node(named.path()) == &named);
      REQUIRE(named["value"].getValueAs<int>() == 1);
    }
  }

  GIVEN("A tree whose node name contained a path separator")
  {
    vsr::core::DataTree tree;
    auto &node = tree.root()["a/b"];

    THEN("The name is sanitized and found again with the original string")
    {
      REQUIRE(node.name() == "a_b");
      REQUIRE(tree.root().child("a/b") == &node);
      REQUIRE(tree.root().child("a_b") == &node);
    }

    THEN("The node's path has a single segment")
    {
      REQUIRE(node.path().str() == "/a_b");
      REQUIRE(tree.node(node.path()) == &node);
    }
  }
}

SCENARIO(
    "vsr::core::DataPath survives a serialization round trip", "[DataPath]")
{
  GIVEN("A tree with an anonymous sequence written to a buffer")
  {
    vsr::core::DataTree source;
    auto &sequence = source.root()["sequence"];
    sequence.append()["name"] = "first";
    sequence.append()["name"] = "second";

    const auto secondPath = sequence.child(size_t(1))->path();
    REQUIRE(secondPath.str() == "/sequence/[1]");

    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer));

    WHEN("The buffer is read back into another tree")
    {
      vsr::core::DataTree destination;
      REQUIRE(destination.read(buffer));

      THEN("The loaded nodes are still addressed by ordinal")
      {
        auto *node = destination.node(secondPath);
        REQUIRE(node != nullptr);
        REQUIRE(node->path() == secondPath);
        REQUIRE(node->child("name")->getValueAs<std::string>() == "second");
      }
    }
  }
}
