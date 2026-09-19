// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#define VSR_DATA_TREE_TEST_MODE
#include "vsr/core/DataTree.hpp"
#include "vsr/core/DataTreeMetadata.hpp"
#include "vsr/core/DataTreeObserver.hpp"
#include "vsr/core/DataTreeText.hpp"
#include "vsr/core/Logging.hpp"
// std
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

SCENARIO("vsr::core::DataTree interface", "[DataTree]")
{
  GIVEN("A normally constructed DataTree")
  {
    vsr::core::DataTree tree;
    auto &root = tree.root();

    THEN("The root node is called 'root'")
    {
      REQUIRE(root.name() == "<root>");
    }

    THEN("The root node has no children")
    {
      REQUIRE(root.numChildren() == 0);
    }

    THEN("The root self() ref must be correct")
    {
      REQUIRE(root.self());
      REQUIRE(root.self().index() == 0);
    }

    WHEN("A child node of the root is accessed with child(i)")
    {
      auto *child = root.child(0);
      THEN("The returned pointer must be null")
      {
        REQUIRE(child == nullptr);
      }
      THEN("The root node still has no children")
      {
        REQUIRE(root.numChildren() == 0);
      }
    }

    WHEN("A child node of the root is accessed with child(name)")
    {
      auto *child = root.child("childNode");
      THEN("The returned pointer must be null")
      {
        REQUIRE(child == nullptr);
      }
      THEN("The root node still has no children")
      {
        REQUIRE(root.numChildren() == 0);
      }
    }

    WHEN("A child node of the root is accessed with operator[](name)")
    {
      auto &child = root["childNode"];
      THEN("The returned child must have the correct name")
      {
        REQUIRE(child.name() == "childNode");
      }
      THEN("The root node now has 1 child")
      {
        REQUIRE(root.numChildren() == 1);
      }

      WHEN("A value is set on the node")
      {
        child = 50;
        THEN("The value returned is the correct type")
        {
          REQUIRE(child.getValue().is<int>());
          REQUIRE(child.getValue().type() == ANARI_INT32);
        }
        THEN("The value returned is the correct value")
        {
          REQUIRE(child.getValueAs<int>() == 50);
        }
      }
    }

    WHEN("A blank child node is appended on the root")
    {
      auto &c1 = root.append();
      THEN("The root node now has 1 child")
      {
        REQUIRE(root.numChildren() == 1);
      }
      THEN("A loop of appending blank nodes creates the correct # of children")
      {
        for (int i = 0; i < 10; i++) {
          c1.append();
          REQUIRE(c1.numChildren() == (i + 1));
        }
      }

      WHEN("Appending a second blank node")
      {
        auto &c2 = root.append();
        THEN("The root node now has 2 children")
        {
          REQUIRE(root.numChildren() == 2);
        }
        THEN(
            "A loop of appending blank nodes creates the correct # of children")
        {
          for (int i = 0; i < 10; i++) {
            c2.append();
            REQUIRE(c2.numChildren() == (i + 1));
          }
        }
      }
    }

    THEN("A loop of appending blank nodes creates the correct # of children")
    {
      for (int i = 0; i < 10; i++) {
        root.append();
        REQUIRE(root.numChildren() == (i + 1));
      }
    }

    WHEN("Accessing multiple layers of nodes all in one go")
    {
      auto &child3a = root["child1a"]["child2a"]["child3a"];
      auto &child3b = root["child1b"]["child2b"]["child3b"];
      THEN("The deeper nodes do not query as null with node.child(i)")
      {
        REQUIRE(root["child1a"].child("child2a") != nullptr);
        REQUIRE(root["child1a"]["child2a"].child("child3a") != nullptr);
        REQUIRE(root["child1b"].child("child2b") != nullptr);
        REQUIRE(root["child1b"]["child2b"].child("child3b") != nullptr);
      }
      THEN("The names of the children are correct")
      {
        REQUIRE(root["child1a"].name() == "child1a");
        REQUIRE(root["child1b"].name() == "child1b");
        REQUIRE(root["child1a"]["child2a"].name() == "child2a");
        REQUIRE(root["child1b"]["child2b"].name() == "child2b");
        REQUIRE(root["child1a"]["child2a"]["child3a"].name() == "child3a");
        REQUIRE(root["child1b"]["child2b"]["child3b"].name() == "child3b");
      }
      THEN("The identity of the deepest nodes are the same")
      {
        REQUIRE(&root["child1a"]["child2a"]["child3a"] == &child3a);
        REQUIRE(&root["child1b"]["child2b"]["child3b"] == &child3b);
      }
      THEN("The root node now has 2 children")
      {
        REQUIRE(root.numChildren() == 2);
      }

      WHEN("Removing a child by name")
      {
        root.remove("child1a");
        THEN("Only the removed child should not exist anymore")
        {
          REQUIRE(root.child("child1a") == nullptr);
          REQUIRE(root.child("child1b") != nullptr);
        }
      }

      WHEN("Removing a child by reference")
      {
        root.remove(root["child1a"]);
        THEN("Only the removed child should not exist anymore")
        {
          REQUIRE(root.child("child1a") == nullptr);
          REQUIRE(root.child("child1b") != nullptr);
        }
      }

      WHEN("Setting a value on an intermediate node")
      {
        root["child1a"]["child2a"] = 100;
        THEN("The original leaf child should no longer exist")
        {
          REQUIRE(root["child1a"]["child2a"].child("child3a") == nullptr);
          REQUIRE(root["child1a"]["child2a"].getValueAs<int>() == 100);
        }
      }
    }

    WHEN("Setting an array as a value")
    {
      int values[5] = {1, 2, 3, 4, 5};

      auto &child = root["arrayChild"];
      child.setValueAsArray(values, 5);

      THEN("The node claims to hold an array")
      {
        REQUIRE(child.holdsArray());
      }

      THEN("The node array storage holds elements of the correct type")
      {
        REQUIRE(child.arrayType() == ANARI_INT32);
      }

      THEN("The node array storage holds the correct size + values")
      {
        // Array storage a tree owns is only handed out const, so that no
        // value can change without an observer seeing it.
        const int *checkedValues = nullptr;
        size_t size = 0;
        const vsr::core::DataNode *constChild = &child;
        constChild->getValueAsArray(&checkedValues, &size);
        REQUIRE(size == 5);
        REQUIRE(checkedValues != nullptr);
        REQUIRE(std::equal(values, values + size, checkedValues));
      }
    }

    WHEN("A subtree is copied between DataTrees")
    {
      auto &destination = root["copied"];
      {
        vsr::core::DataTree sourceTree;
        auto &source = sourceTree.root()["source"];
        source["child"]["grandchild"] = 42;
        destination = source;
      }

      THEN("The copied node can still access copied children")
      {
        REQUIRE(destination.child("child") != nullptr);
        REQUIRE(destination["child"].child("grandchild") != nullptr);
        REQUIRE(destination["child"]["grandchild"].getValueAs<int>() == 42);
      }

      THEN("The copied node keeps its own name, not the source's")
      {
        REQUIRE(destination.name() == "copied");
        REQUIRE(root.child("copied") == &destination);
      }

      THEN("The copied node owns an independent subtree")
      {
        destination["child"]["grandchild"] = 50;
        REQUIRE(destination["child"]["grandchild"].getValueAs<int>() == 50);
      }
    }
  }
}

SCENARIO("DataTree values round trip through a byte buffer", "[DataTree]")
{
  vsr::core::DataTree source;
  source.root()["settings"]["sampleCount"] = 16;
  const float weights[] = {0.25f, 0.5f, 0.75f};
  source.root()["weights"].setValueAsArray(weights, 3);

  std::vector<std::byte> buffer;
  REQUIRE(source.write(buffer));
  REQUIRE_FALSE(buffer.empty());

  vsr::core::DataTree destination;
  REQUIRE(destination.read(buffer));

  REQUIRE(
      destination.root()["settings"]["sampleCount"].getValueAs<int>() == 16);
  const float *roundTripWeights = nullptr;
  size_t numWeights = 0;
  const auto *weightsNode = destination.root().child("weights");
  REQUIRE(weightsNode != nullptr);
  weightsNode->getValueAsArray(&roundTripWeights, &numWeights);
  REQUIRE(numWeights == 3);
  REQUIRE(std::equal(weights, weights + numWeights, roundTripWeights));
}

SCENARIO("vsr::core::DataTree metadata helpers", "[DataTree]")
{
  GIVEN("An empty DataTree")
  {
    vsr::core::DataTree tree;
    auto &root = tree.root();

    THEN("metadata is reported as missing")
    {
      auto result = vsr::core::readDataTreeMetadata(root);
      REQUIRE(result.status == vsr::core::DataTreeMetadataReadStatus::Missing);
      REQUIRE(!result.metadata);
    }

    WHEN("metadata is written")
    {
      vsr::core::writeDataTreeMetadata(root, {1, "scene", "vsr.scene.full", 1});

      THEN("the required fields can be read back")
      {
        auto result = vsr::core::readDataTreeMetadata(root);
        REQUIRE(result.status == vsr::core::DataTreeMetadataReadStatus::Found);
        REQUIRE(result.metadata);
        REQUIRE(result.metadata->envelopeVersion == 1);
        REQUIRE(result.metadata->fileType == "scene");
        REQUIRE(result.metadata->schema == "vsr.scene.full");
        REQUIRE(result.metadata->schemaVersion == 1);
      }
    }

    WHEN("optional metadata is present")
    {
      root[vsr::core::DATA_TREE_METADATA_NODE]["producer"] = "test";
      vsr::core::writeDataTreeMetadata(root, {1, "scene", "vsr.scene.full", 1});

      THEN("writing required fields preserves optional fields")
      {
        auto *producer =
            root[vsr::core::DATA_TREE_METADATA_NODE].child("producer");
        REQUIRE(producer != nullptr);
        REQUIRE(producer->getValueAs<std::string>() == "test");
      }
    }

    WHEN("metadata is present but incomplete")
    {
      root[vsr::core::DATA_TREE_METADATA_NODE]["schema"] = "vsr.scene.full";

      THEN("the metadata is rejected as malformed")
      {
        auto result = vsr::core::readDataTreeMetadata(root);
        REQUIRE(
            result.status == vsr::core::DataTreeMetadataReadStatus::Malformed);
        REQUIRE(result.message.find("envelopeVersion") != std::string::npos);
      }
    }

    WHEN("metadata uses the pre-rename __tsd_metadata node")
    {
      auto &metadata = root[vsr::core::LEGACY_DATA_TREE_METADATA_NODE];
      metadata["envelopeVersion"] = 1;
      metadata["fileType"] = "scene";
      metadata["schema"] = "tsd.scene.full";
      metadata["schemaVersion"] = 1;

      THEN("it is read and its schema is normalized to the vsr spelling")
      {
        auto result = vsr::core::readDataTreeMetadata(root);
        REQUIRE(result.status == vsr::core::DataTreeMetadataReadStatus::Found);
        REQUIRE(result.metadata);
        REQUIRE(result.metadata->fileType == "scene");
        REQUIRE(result.metadata->schema == "vsr.scene.full");
        REQUIRE(result.metadata->schemaVersion == 1);
      }
    }

    WHEN("a legacy __tsd_metadata node is incomplete")
    {
      root[vsr::core::LEGACY_DATA_TREE_METADATA_NODE]["schema"] =
          "tsd.scene.full";

      THEN("the malformed message names the legacy node")
      {
        auto result = vsr::core::readDataTreeMetadata(root);
        REQUIRE(
            result.status == vsr::core::DataTreeMetadataReadStatus::Malformed);
        REQUIRE(result.message.find(vsr::core::LEGACY_DATA_TREE_METADATA_NODE)
            != std::string::npos);
      }
    }

    WHEN("both metadata nodes are present")
    {
      auto &legacy = root[vsr::core::LEGACY_DATA_TREE_METADATA_NODE];
      legacy["envelopeVersion"] = 1;
      legacy["fileType"] = "scene";
      legacy["schema"] = "tsd.scene.cameras";
      legacy["schemaVersion"] = 1;
      vsr::core::writeDataTreeMetadata(root, {1, "scene", "vsr.scene.full", 3});

      THEN("the current node wins")
      {
        auto result = vsr::core::readDataTreeMetadata(root);
        REQUIRE(result.status == vsr::core::DataTreeMetadataReadStatus::Found);
        REQUIRE(result.metadata->schema == "vsr.scene.full");
        REQUIRE(result.metadata->schemaVersion == 3);
      }
    }

    WHEN("metadata uses the wrong required field type")
    {
      auto &metadata = root[vsr::core::DATA_TREE_METADATA_NODE];
      metadata["envelopeVersion"] = "1";
      metadata["fileType"] = "scene";
      metadata["schema"] = "vsr.scene.full";
      metadata["schemaVersion"] = 1;

      THEN("the metadata is rejected as malformed")
      {
        auto result = vsr::core::readDataTreeMetadata(root);
        REQUIRE(
            result.status == vsr::core::DataTreeMetadataReadStatus::Malformed);
        REQUIRE(result.message.find("envelopeVersion") != std::string::npos);
        REQUIRE(result.message.find("got") != std::string::npos);
      }
    }
  }
}

// Change notification /////////////////////////////////////////////////////////

namespace {

struct RecordedSignal
{
  enum Kind
  {
    ValueChanged,
    ValueCleared,
    NodeAdded,
    NodeRemoved,
    SubtreeReplaced,
    BatchBegin,
    BatchEnd
  };

  Kind kind{ValueChanged};
  std::string path;
  std::string value;
  size_t numDescendants{0};
  size_t numChildren{0};
  size_t numTraversed{0};
};

struct RecordingObserver : vsr::core::DataTreeObserver
{
  void signalValueChanged(const vsr::core::DataNode &n) override
  {
    record(RecordedSignal::ValueChanged, n);
  }

  void signalValueCleared(const vsr::core::DataNode &n) override
  {
    record(RecordedSignal::ValueCleared, n);
  }

  void signalNodeAdded(const vsr::core::DataNode &n) override
  {
    record(RecordedSignal::NodeAdded, n);
  }

  void signalNodeRemoved(const vsr::core::DataNode &n) override
  {
    auto &recorded = record(RecordedSignal::NodeRemoved, n);
    n.forall_children_const(
        [&](const vsr::core::DataNode &) { recorded.numDescendants++; });
    n.foreach_child_const(
        [&](const vsr::core::DataNode &) { recorded.numChildren++; });
    n.traverse_const([&](const vsr::core::DataNode &, int) {
      recorded.numTraversed++;
      return true;
    });
  }

  void signalSubtreeReplaced(const vsr::core::DataNode &n) override
  {
    record(RecordedSignal::SubtreeReplaced, n);
  }

  void signalUpdateBatchBegin() override
  {
    signals.push_back({RecordedSignal::BatchBegin, "", "", 0, 0, 0});
  }

  void signalUpdateBatchEnd() override
  {
    signals.push_back({RecordedSignal::BatchEnd, "", "", 0, 0, 0});
  }

  size_t count(RecordedSignal::Kind kind) const
  {
    return size_t(std::count_if(signals.begin(),
        signals.end(),
        [&](const RecordedSignal &s) { return s.kind == kind; }));
  }

  std::vector<RecordedSignal> signals;

 private:
  RecordedSignal &record(
      RecordedSignal::Kind kind, const vsr::core::DataNode &n)
  {
    signals.push_back({kind,
        n.path().str(),
        n.getValue().is(ANARI_INT32) ? std::to_string(n.getValueAs<int>()) : "",
        0,
        0,
        0});
    return signals.back();
  }
};

} // namespace

SCENARIO("vsr::core::DataTree change notification", "[DataTree]")
{
  GIVEN("A DataTree with a recording observer installed")
  {
    vsr::core::DataTree tree;
    RecordingObserver observer;
    tree.setObserver(&observer);
    auto &root = tree.root();

    THEN("The observer can be read back and removed again")
    {
      REQUIRE(tree.observer() == &observer);
      tree.setObserver(nullptr);
      REQUIRE(tree.observer() == nullptr);
      root["quiet"] = 1;
      REQUIRE(observer.signals.empty());
    }

    WHEN("A node is added")
    {
      auto &child = root["width"];

      THEN("One node-added signal carrying the node's path arrives")
      {
        REQUIRE(observer.signals.size() == 1);
        REQUIRE(observer.signals[0].kind == RecordedSignal::NodeAdded);
        REQUIRE(observer.signals[0].path == "/width");
      }

      THEN("Accessing the same node again adds no further signal")
      {
        REQUIRE(root.child("width") == &child);
        REQUIRE(&root["width"] == &child);
        REQUIRE(observer.signals.size() == 1);
      }

      WHEN("A value is set on it")
      {
        child = 1920;

        THEN("A value-changed signal carrying the new value arrives")
        {
          REQUIRE(observer.signals.size() == 2);
          REQUIRE(observer.signals[1].kind == RecordedSignal::ValueChanged);
          REQUIRE(observer.signals[1].path == "/width");
          REQUIRE(observer.signals[1].value == "1920");
        }

        WHEN("Its value is cleared")
        {
          child.clearValue();

          THEN("A value-cleared signal arrives")
          {
            REQUIRE(observer.signals.size() == 3);
            REQUIRE(observer.signals[2].kind == RecordedSignal::ValueCleared);
            REQUIRE(observer.signals[2].path == "/width");
          }

          THEN("Clearing an already empty value signals nothing further")
          {
            const auto numSignals = observer.signals.size();
            child.clearValue();
            REQUIRE(observer.signals.size() == numSignals);
          }
        }
      }
    }

    WHEN("An array value is set")
    {
      const int values[] = {1, 2, 3};
      root["data"].setValueAsArray(values, 3);

      THEN("Array data a tree owns signals like a scalar value does")
      {
        REQUIRE(observer.count(RecordedSignal::ValueChanged) == 1);
        REQUIRE(observer.signals.back().path == "/data");
      }
    }

    WHEN("An external array is set and then changed behind the tree's back")
    {
      int values[] = {1, 2, 3};
      auto &node = root["external"];
      node.setValueAsExternalArray(ANARI_INT32, values, 3);
      const auto afterSet = observer.count(RecordedSignal::ValueChanged);

      values[0] = 7;
      node.signalExternalArrayChanged();

      THEN("The explicit signal is what produces the notification")
      {
        REQUIRE(afterSet == 1);
        REQUIRE(observer.count(RecordedSignal::ValueChanged) == 2);
        REQUIRE(observer.signals.back().path == "/external");
      }
    }

    WHEN("An interior node with a subtree beneath it is removed")
    {
      auto &group = root["group"];
      group["a"]["deep"] = 1;
      group["b"] = 2;
      observer.signals.clear();

      root.remove("group");

      THEN("Exactly one removal signal arrives, about the node itself")
      {
        REQUIRE(observer.count(RecordedSignal::NodeRemoved) == 1);
        REQUIRE(observer.signals[0].kind == RecordedSignal::NodeRemoved);
        REQUIRE(observer.signals[0].path == "/group");
      }

      THEN("The doomed subtree was still traversable when it was delivered")
      {
        REQUIRE(observer.signals[0].numDescendants == 3);
        REQUIRE(observer.signals[0].numChildren == 2);
        REQUIRE(observer.signals[0].numTraversed == 4); // the node plus its 3
      }

      THEN("The node is gone once the removal returns")
      {
        REQUIRE(root.child("group") == nullptr);
      }
    }

    WHEN("A node is removed by reference")
    {
      root["doomed"] = 1;
      observer.signals.clear();

      root.remove(root["doomed"]);

      THEN("A removal signal arrives for it")
      {
        REQUIRE(observer.count(RecordedSignal::NodeRemoved) == 1);
        REQUIRE(observer.signals[0].path == "/doomed");
      }
    }

    WHEN("A node that is not a child is passed to remove()")
    {
      auto &owner = root["owner"];
      auto &stranger = root["stranger"];
      observer.signals.clear();

      owner.remove(stranger);

      THEN("No removal is announced for a node that was never a child")
      {
        REQUIRE(observer.count(RecordedSignal::NodeRemoved) == 0);
      }
    }

    WHEN("A node holding children is given a value instead")
    {
      root["group"]["a"] = 1;
      root["group"]["b"] = 2;
      observer.signals.clear();

      root["group"] = 3;

      THEN("The displaced children are reported as removed")
      {
        REQUIRE(observer.count(RecordedSignal::NodeRemoved) == 2);
        REQUIRE(observer.count(RecordedSignal::ValueChanged) == 1);
        REQUIRE(observer.signals.back().kind == RecordedSignal::ValueChanged);
      }
    }
  }
}

SCENARIO("vsr::core::DataTree update batches", "[DataTree]")
{
  GIVEN("A DataTree with a recording observer installed")
  {
    vsr::core::DataTree tree;
    RecordingObserver observer;
    tree.setObserver(&observer);
    auto &root = tree.root();

    WHEN("A run of edits is bracketed by a scoped batch")
    {
      {
        vsr::core::DataTreeUpdateBatch batch(tree);
        root["a"] = 1;
        root["b"] = 2;
      }

      THEN("One begin arrives first and one end arrives last")
      {
        REQUIRE(observer.signals.size() > 2);
        REQUIRE(observer.signals.front().kind == RecordedSignal::BatchBegin);
        REQUIRE(observer.signals.back().kind == RecordedSignal::BatchEnd);
        REQUIRE(observer.count(RecordedSignal::BatchBegin) == 1);
        REQUIRE(observer.count(RecordedSignal::BatchEnd) == 1);
      }

      THEN("The individual signals still arrive inside the bracket")
      {
        REQUIRE(observer.count(RecordedSignal::NodeAdded) == 2);
        REQUIRE(observer.count(RecordedSignal::ValueChanged) == 2);
      }
    }

    WHEN("Batches are nested")
    {
      tree.beginUpdateBatch();
      {
        vsr::core::DataTreeUpdateBatch inner(tree);
        root["a"] = 1;
      }

      THEN("The inner batch does not end the outer one")
      {
        REQUIRE(observer.count(RecordedSignal::BatchEnd) == 0);
      }

      tree.endUpdateBatch();

      THEN("Only the outermost bracket is reported")
      {
        REQUIRE(observer.count(RecordedSignal::BatchBegin) == 1);
        REQUIRE(observer.count(RecordedSignal::BatchEnd) == 1);
      }
    }

    WHEN("A subtree is copy-assigned onto a node")
    {
      vsr::core::DataTree source;
      source.root()["child"]["grandchild"] = 42;
      source.root()["other"] = 7;
      observer.signals.clear();

      root["copied"] = source.root();

      THEN("The copy brackets itself, once, however deep it recurses")
      {
        REQUIRE(observer.count(RecordedSignal::BatchBegin) == 1);
        REQUIRE(observer.count(RecordedSignal::BatchEnd) == 1);
        REQUIRE(observer.signals.back().kind == RecordedSignal::BatchEnd);
      }

      THEN("The copied nodes are each reported as added")
      {
        REQUIRE(observer.count(RecordedSignal::NodeAdded) >= 4);
      }
    }

    WHEN("A node is reset")
    {
      root["group"]["a"] = 1;
      observer.signals.clear();

      root["group"].reset();

      THEN("The reset does not bracket itself")
      {
        REQUIRE(observer.count(RecordedSignal::BatchBegin) == 0);
        REQUIRE(observer.count(RecordedSignal::BatchEnd) == 0);
      }

      THEN("Its children are still reported as removed")
      {
        REQUIRE(observer.count(RecordedSignal::NodeRemoved) == 1);
      }
    }
  }
}

SCENARIO("vsr::core::DataTree collapses a read to one signal", "[DataTree]")
{
  GIVEN("A serialized tree of many nodes")
  {
    vsr::core::DataTree source;
    for (int i = 0; i < 32; i++)
      source.root()["values"][std::to_string(i)] = i;

    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer));

    WHEN("It is read into an observed tree")
    {
      vsr::core::DataTree destination;
      RecordingObserver observer;
      destination.setObserver(&observer);

      REQUIRE(destination.read(buffer));

      THEN("Only the subtree-replaced signal arrives, naming the root")
      {
        REQUIRE(observer.signals.size() == 1);
        REQUIRE(observer.signals[0].kind == RecordedSignal::SubtreeReplaced);
        REQUIRE(vsr::core::DataPath(observer.signals[0].path).isRoot());
      }

      THEN("The tree really was populated")
      {
        REQUIRE(destination.root()["values"].numChildren() == 32);
        REQUIRE(destination.root()["values"]["7"].getValueAs<int>() == 7);
      }
    }
  }
}

SCENARIO("vsr::core::DataNode names are sanitized, not rejected", "[DataTree]")
{
  GIVEN("A DataTree")
  {
    vsr::core::DataTree tree;
    auto &root = tree.root();

    WHEN("A repaired name is appended repeatedly")
    {
      std::vector<std::string> warnings;
      vsr::core::setLoggingCallback(
          [&](vsr::core::LogLevel level, std::string message) {
            if (level == vsr::core::WARNING)
              warnings.push_back(std::move(message));
          });

      // A name unique to this test: the warning fires once per distinct
      // offending name for the life of the process.
      for (int i = 0; i < 3; i++)
        root.append("warn/once");
      root.child("warn/once");

      vsr::core::setNoLogging();

      THEN("The warning is emitted once, and names the repair")
      {
        REQUIRE(warnings.size() == 1);
        REQUIRE(warnings[0].find("warn/once") != std::string::npos);
        REQUIRE(warnings[0].find("warn_once") != std::string::npos);
      }
    }

    WHEN("A node is appended with a name containing the path separator")
    {
      auto &node = root.append("units/mm");

      THEN("The separator is replaced rather than the write failing")
      {
        REQUIRE(node.name() == "units_mm");
        REQUIRE(root.numChildren() == 1);
      }

      THEN("Lookup with the original string finds the same node")
      {
        REQUIRE(root.child("units/mm") == &node);
        REQUIRE(&root["units/mm"] == &node);
        REQUIRE(root.numChildren() == 1);
      }

      THEN("Removal with the original string finds it too")
      {
        root.remove("units/mm");
        REQUIRE(root.numChildren() == 0);
      }

      THEN("A name that sanitizes onto an existing node aliases it")
      {
        REQUIRE(&root["units_mm"] == &node);
        REQUIRE(root.numChildren() == 1);
      }
    }
  }
}

SCENARIO("A DataNode serializes as the tree it roots", "[DataTree]")
{
  GIVEN("A subtree written from an interior node")
  {
    vsr::core::DataTree source;
    source.root()["material"]["roughness"] = 0.4f;
    source.root()["material"]["layers"]["base"] = 7;
    source.root()["unrelated"] = 1;

    std::vector<std::byte> buffer;
    REQUIRE(source.root()["material"].write(buffer));

    WHEN("It is read into a differently named node of another tree")
    {
      vsr::core::DataTree destination;
      REQUIRE(destination.root()["surface"].read(buffer));

      THEN("The subtree arrives beneath the node that read it")
      {
        REQUIRE(destination.root()["surface"]["roughness"].getValueAs<float>()
            == 0.4f);
        REQUIRE(
            destination.root()["surface"]["layers"]["base"].getValueAs<int>()
            == 7);
      }

      THEN("The source node's own name did not travel with its bytes")
      {
        REQUIRE(destination.root().child("material") == nullptr);
      }

      THEN("Nothing outside the written subtree came along")
      {
        REQUIRE(destination.root().child("unrelated") == nullptr);
      }
    }

    WHEN("It is read into the root of another tree")
    {
      vsr::core::DataTree destination;
      REQUIRE(destination.read(buffer));

      THEN("The subtree becomes that tree's whole contents")
      {
        REQUIRE(destination.root()["roughness"].getValueAs<float>() == 0.4f);
        REQUIRE(destination.root().child("material") == nullptr);
      }
    }
  }

  GIVEN("A leaf node, which roots an empty tree")
  {
    vsr::core::DataTree source;
    source.root()["count"] = 16;

    std::vector<std::byte> buffer;
    REQUIRE(source.root()["count"].write(buffer));

    WHEN("Its bytes are read back")
    {
      vsr::core::DataTree destination;
      destination.root()["stale"] = 1;
      REQUIRE(destination.read(buffer));

      THEN("A valid, empty tree comes back rather than the leaf's value")
      {
        REQUIRE_FALSE(buffer.empty());
        REQUIRE(destination.root().numChildren() == 0);
      }
    }
  }
}

SCENARIO("Reading into a DataNode replaces what it held", "[DataTree]")
{
  GIVEN("A serialized subtree and a node that already has contents")
  {
    vsr::core::DataTree source;
    source.root()["fromFile"] = 1;

    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer));

    vsr::core::DataTree destination;
    destination.root()["target"]["keep"] = 2;
    destination.root()["target"]["alsoKeep"] = 3;

    WHEN("The subtree is read into that node")
    {
      REQUIRE(destination.root()["target"].read(buffer));

      THEN("What the node held is gone, not merged with")
      {
        REQUIRE(destination.root()["target"].child("keep") == nullptr);
        REQUIRE(destination.root()["target"].child("alsoKeep") == nullptr);
        REQUIRE(
            destination.root()["target"]["fromFile"].getValueAs<int>() == 1);
        REQUIRE(destination.root()["target"].numChildren() == 1);
      }
    }
  }

  GIVEN("A node holding a value rather than children")
  {
    vsr::core::DataTree source;
    source.root()["fromFile"] = 1;

    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer));

    vsr::core::DataTree destination;
    destination.root()["target"] = 99;

    WHEN("A subtree is read into it")
    {
      REQUIRE(destination.root()["target"].read(buffer));

      THEN("Its value is gone too: the node holds exactly what was read")
      {
        REQUIRE(destination.root()["target"].empty());
        REQUIRE(
            destination.root()["target"]["fromFile"].getValueAs<int>() == 1);
      }
    }
  }
}

SCENARIO("A corrupt buffer is rejected without allocating for it", "[DataTree]")
{
  GIVEN("A buffer whose leaf count and lengths are garbage")
  {
    std::vector<std::byte> buffer;
    for (int i = 0; i < 64; ++i)
      buffer.push_back(std::byte(0xA5 ^ (i * 37)));

    WHEN("It is read into a node")
    {
      vsr::core::DataTree destination;
      bool ok = true;

      THEN("The read fails instead of throwing")
      {
        REQUIRE_NOTHROW(ok = destination.root().read(buffer));
        REQUIRE_FALSE(ok);
        REQUIRE(destination.root().numChildren() == 0);
      }
    }
  }

  GIVEN("A valid buffer whose first string length is overwritten")
  {
    vsr::core::DataTree source;
    source.root()["a"]["b"] = 1;
    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer));
    const size_t hugeLength = size_t(1) << 60;
    std::memcpy(buffer.data() + sizeof(size_t), &hugeLength, sizeof(size_t));

    THEN("The read fails instead of throwing")
    {
      vsr::core::DataTree destination;
      bool ok = true;
      REQUIRE_NOTHROW(ok = destination.root().read(buffer));
      REQUIRE_FALSE(ok);
    }
  }
}

SCENARIO("A failed read leaves the node empty", "[DataTree]")
{
  GIVEN("A truncated buffer and a populated node")
  {
    vsr::core::DataTree source;
    for (int i = 0; i < 32; i++)
      source.root()["values"][std::to_string(i)] = i;

    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer));
    buffer.resize(buffer.size() / 2);

    vsr::core::DataTree destination;
    destination.root()["target"]["keep"] = 1;

    WHEN("The truncated buffer is read into the node")
    {
      const bool ok = destination.root()["target"].read(buffer);

      THEN("The read reports failure")
      {
        REQUIRE_FALSE(ok);
      }

      THEN("No half-built subtree is left behind")
      {
        REQUIRE(destination.root()["target"].numChildren() == 0);
        REQUIRE(destination.root()["target"].empty());
      }
    }

    WHEN("An observed node fails to read")
    {
      RecordingObserver observer;
      destination.setObserver(&observer);

      REQUIRE_FALSE(destination.root()["target"].read(buffer));

      THEN("One subtree-replaced signal still arrives: the node did change")
      {
        REQUIRE(observer.signals.size() == 1);
        REQUIRE(observer.signals[0].kind == RecordedSignal::SubtreeReplaced);
        REQUIRE(observer.signals[0].path == "/target");
      }
    }
  }
}

SCENARIO("A DataNode with no tree behind it refuses to read", "[DataTree]")
{
  GIVEN("A serialized tree and a detached DataNode")
  {
    vsr::core::DataTree source;
    source.root()["value"] = 1;

    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer));

    vsr::core::DataNode detached;

    WHEN("The buffer is read into it")
    {
      THEN("The read fails rather than dereferencing nothing")
      {
        REQUIRE_FALSE(detached.read(buffer));
      }
    }

    WHEN("It is written")
    {
      std::vector<std::byte> detachedBuffer;

      THEN("It writes the empty tree it roots")
      {
        REQUIRE(detached.write(detachedBuffer));

        vsr::core::DataTree destination;
        destination.root()["stale"] = 1;
        REQUIRE(destination.read(detachedBuffer));
        REQUIRE(destination.root().numChildren() == 0);
      }
    }
  }
}

SCENARIO("Reading a subtree signals only the node that read it", "[DataTree]")
{
  GIVEN("An observed tree with two independent subtrees")
  {
    vsr::core::DataTree source;
    for (int i = 0; i < 8; i++)
      source.root()[std::to_string(i)] = i;

    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer));

    vsr::core::DataTree destination;
    destination.root()["left"]["old"] = 1;
    destination.root()["right"]["untouched"] = 2;

    RecordingObserver observer;
    destination.setObserver(&observer);

    WHEN("One of them is read into")
    {
      REQUIRE(destination.root()["left"].read(buffer));

      THEN("Exactly one signal arrives, naming the node that was replaced")
      {
        REQUIRE(observer.signals.size() == 1);
        REQUIRE(observer.signals[0].kind == RecordedSignal::SubtreeReplaced);
        REQUIRE(observer.signals[0].path == "/left");
      }

      THEN("The other subtree is untouched")
      {
        REQUIRE(
            destination.root()["right"]["untouched"].getValueAs<int>() == 2);
      }
    }
  }
}

SCENARIO("Loaded anonymous names are claimed against the counter", "[DataTree]")
{
  GIVEN("A subtree carrying an anonymous name this process has not minted yet")
  {
    // A name minted by some other process's counter: ahead of this one, which
    // is exactly the case a subtree read from a file presents.
    const int reach = vsr::core::anonymousNodeCounter() + 5;
    const std::string futureName = "<" + std::to_string(reach) + ">";

    vsr::core::DataTree source;
    source.root()["items"][futureName] = 1;

    std::vector<std::byte> buffer;
    REQUIRE(source.root()["items"].write(buffer));

    WHEN("It is read in and anonymous nodes are appended afterwards")
    {
      vsr::core::DataTree destination;
      auto &items = destination.root()["items"];
      REQUIRE(items.read(buffer));
      REQUIRE(items.numChildren() == 1);

      // Enough appends to walk the counter past where the loaded name sits.
      constexpr size_t NUM_APPENDS = 8;
      for (size_t i = 0; i < NUM_APPENDS; i++)
        items.append();

      THEN("Every append produced a new node rather than a loaded one")
      {
        REQUIRE(items.numChildren() == 1 + NUM_APPENDS);
        REQUIRE(items[futureName].getValueAs<int>() == 1);
      }
    }
  }
}

SCENARIO(
    "A DataReader tells an empty source from an unsizeable one", "[DataTree]")
{
  GIVEN("an empty buffer")
  {
    const std::vector<std::byte> empty;
    vsr::core::BufferReader reader(empty);
    THEN("no bytes remain, and that is known")
    {
      REQUIRE(reader.bytesRemaining() == size_t(0));
    }
  }

  GIVEN("an empty file")
  {
    const auto path = std::filesystem::temp_directory_path()
        / "vsr_test_DataTree_empty_reader.bin";
    std::fclose(std::fopen(path.string().c_str(), "wb"));
    {
      vsr::core::FileReader reader(path.string().c_str());
      REQUIRE(reader.valid());
      THEN("no bytes remain, and that is known")
      {
        REQUIRE(reader.bytesRemaining() == size_t(0));
      }
    }
    std::filesystem::remove(path);
  }

  GIVEN("a reader that cannot be sized")
  {
    struct Unsized : public vsr::core::DataReader
    {
      size_t read(void *, size_t, size_t) override
      {
        return 0;
      }
    } reader;
    THEN("the remaining count is unknown rather than a sentinel")
    {
      REQUIRE_FALSE(reader.bytesRemaining().has_value());
    }
  }
}

// Text Encoding ///////////////////////////////////////////////////////////////

namespace {

std::string_view asText(const std::vector<std::byte> &buffer)
{
  return std::string_view(
      reinterpret_cast<const char *>(buffer.data()), buffer.size());
}

// Warnings the Text Encoding reader logs while the recorder is alive.
struct WarningRecorder
{
  WarningRecorder()
  {
    vsr::core::setLoggingCallback(
        [this](vsr::core::LogLevel level, std::string message) {
          if (level == vsr::core::WARNING)
            warnings.push_back(std::move(message));
        });
  }
  ~WarningRecorder()
  {
    vsr::core::setNoLogging();
  }

  bool namesLineAndColumn() const
  {
    return warnings.size() == 1
        && warnings[0].find("line ") != std::string::npos
        && warnings[0].find("column ") != std::string::npos;
  }

  std::vector<std::string> warnings;
};

// The hand-written sample: every type category and grammar corner, in the
// writer's canonical form, so that loading and re-saving it is byte-identical.
constexpr const char *TEXT_SAMPLE = R"(vsr-text 1
flags {
  enabled = true
  hidden = false
}
integers {
  i8 = int8 -128
  u8 = uint8 255
  i16 = int16 -32768
  u16 = uint16 65535
  i32 = int32 -2147483648
  u32 = uint32 4294967295
  i64 = int64 -9223372036854775808
  u64 = uint64 18446744073709551615
  kind = data_type 1068
}
fixed {
  f8 = fixed8 -127
  uf8 = ufixed8 200
  rgba = ufixed8_rgba_srgb 255 128 64 255
  f16 = fixed16_vec2 -32000 32000
  uf32 = ufixed32 4000000000
}
floats {
  half = float16 0.5
  single = float32 0.1
  negative = float32 -3.25
  huge = float32 1e+30
  tiny = float64 1e-300
  precise = float64 0.1
  notANumber = float32 nan
  infinite = float64 inf
  negInfinite = float32 -inf
}
vectors {
  position = float32_vec3 0 1.5 -4
  color = float32_vec4 1 0.5 0.25 1
  size = int32_vec2 1920 1080
  bounds = float32_box3 -1 -1 -1 1 1 1
  region = uint64_region2 0 0 10 10
  rotation = float32_quat_ijkw 0 0 0 1
}
matrices {
  transform = float32_mat4 1 0 0 0 0 1 0 0 0 0 1 0 5 6 7 1
  small = float32_mat2x3 1 2 3 4 5 6
}
strings {
  plain = "hello"
  escaped = "quote \" backslash \\ newline \n tab \t bell \x07"
  utf8 = "grüße"
  empty = ""
}
references {
  geometry = geometry@12
  material = material@0
  array = array1d@3
}
arrays {
  bools = bool[] [
    true false true
  ]
  bytes = uint8[] [
    0 1 2 3 4 5 6 7
    8 9
  ]
  halves = float16[] [
    0.5 1 1.5
  ]
  floats = float32[] [
    0.25 0.5 0.75
  ]
  doubles = float64[] [
    0.1 0.2
  ]
  bigInts = int64[] [
    -1 9223372036854775807
  ]
  positions = float32_vec3[] [
    0 0 0
    1 0 0
    0 1 0
  ]
  transforms = float32_mat4[] [
    1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1
  ]
  references = geometry[] [
    geometry@1 geometry@2
  ]
  empty = float32[] []
}
keyed {
  1 = int32 1
  2 = int32 2
}
sequence {
  - = float32 0
  - = float32 1 {
    label = "first keyframe"
  }
  - {
    label = "no value"
  }
  - {}
}
"needs quoting" {
  "tab\tname" = int32 1
  "quote\"name" = int32 2
  "-" = int32 3
  "a+b" = int32 4
  "spaced out" = int32 5
}
interior = int32 7 {
  child = int32 8
}
valueless {}
)";

// The same tree spelled the way a person might: comments everywhere they are
// legal, free whitespace, and the explicit forms of the two omissible types.
constexpr const char *TEXT_SAMPLE_COMMENTED = R"(vsr-text 1   # header comment
# A leading comment line.

flags {   # trailing comment on a block
  enabled = bool true   # explicit bool type
  hidden = false
}
integers { i8 = int8 -128 u8 = uint8 255 i16 = int16 -32768
  u16 = uint16 65535 i32 = int32 -2147483648 u32 = uint32 4294967295
  i64 = int64 -9223372036854775808 u64 = uint64 18446744073709551615
  kind = data_type 1068 }
fixed {
  f8 = fixed8 -127
  uf8 = ufixed8 200
  rgba = ufixed8_rgba_srgb 255 128 64 255
  f16 = fixed16_vec2 -32000
                     32000       # components may span lines
  uf32 = ufixed32 4000000000
}
floats {
  half = float16 0.5
  single = float32 0.1
  negative = float32 -3.25
  huge = float32 1E30            # any exponent spelling parses
  tiny = float64 1e-300
  precise = float64 0.10000000000000001
  notANumber = float32 nan
  infinite = float64 inf
  negInfinite = float32 -inf
}
vectors {
  position = float32_vec3 0 1.5 -4
  color = float32_vec4 1 0.5 0.25 1
  size = int32_vec2 1920 1080
  bounds = float32_box3 -1 -1 -1 1 1 1
  region = uint64_region2 0 0 10 10
  rotation = float32_quat_ijkw 0 0 0 1
}
matrices {
  transform = float32_mat4
    1 0 0 0
    0 1 0 0
    0 0 1 0
    5 6 7 1
  small = float32_mat2x3 1 2 3 4 5 6
}
strings {
  plain = string "hello"        # explicit string type
  escaped = "quote \" backslash \\ newline \n tab \t bell \x07"
  utf8 = "grüße"
  empty = ""
}
references {
  geometry = geometry @ 12      # whitespace around '@' is insignificant
  material = material@0
  array = array1d@3
}
arrays {
  bools = bool[] [ true false true ]
  bytes = uint8[] [ 0 1 2 3 4 5 6 7 8 9 ]
  halves = float16[] [ 0.5 1 1.5 ]
  floats = float32 [] [ 0.25 0.5 0.75 ]
  doubles = float64[] [ 0.1 0.2 ]
  bigInts = int64[] [ -1 9223372036854775807 ]
  positions = float32_vec3[] [
    0 0 0   # origin
    1 0 0   # x
    0 1 0   # y
  ]
  transforms = float32_mat4[] [ 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 ]
  references = geometry[] [
    geometry@1
    geometry@2
  ]
  empty = float32[] [
  ]
}
keyed {
  1 = int32 1
  2 = int32 2
}
sequence {
  - = float32 0
  - = float32 1 { label = "first keyframe" }
  - { label = "no value" }
  - {}
}
"needs quoting" {
  "tab\tname" = int32 1
  "quote\"name" = int32 2
  "-" = int32 3
  "a+b" = int32 4
  "spaced out" = int32 5
}
interior = int32 7 { child = int32 8 }
valueless {}
# A trailing comment.
)";

} // namespace

SCENARIO("DataTree values round trip through the Text Encoding", "[DataTree]")
{
  vsr::core::DataTree source;
  source.root()["settings"]["sampleCount"] = 16;
  const float weights[] = {0.25f, 0.5f, 0.75f};
  source.root()["weights"].setValueAsArray(weights, 3);

  WHEN("The tree is written as text")
  {
    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer, vsr::core::Encoding::Text));

    THEN("The buffer begins with the header and is the string form")
    {
      REQUIRE(vsr::core::isDataTreeText(asText(buffer)));
      REQUIRE(asText(buffer) == source.toText());
      REQUIRE(source.toText().rfind("vsr-text 1\n", 0) == 0);
      REQUIRE(source.toText().back() == '\n');
    }

    THEN("Reading it back needs no encoding argument")
    {
      vsr::core::DataTree destination;
      REQUIRE(destination.read(buffer));
      REQUIRE(destination.root()["settings"]["sampleCount"].getValueAs<int>()
          == 16);

      const float *roundTripWeights = nullptr;
      size_t numWeights = 0;
      const auto *weightsNode = destination.root().child("weights");
      REQUIRE(weightsNode != nullptr);
      weightsNode->getValueAsArray(&roundTripWeights, &numWeights);
      REQUIRE(numWeights == 3);
      REQUIRE(std::equal(weights, weights + numWeights, roundTripWeights));
    }

    THEN("The default encoding is still binary")
    {
      std::vector<std::byte> binary;
      REQUIRE(source.write(binary));
      REQUIRE_FALSE(vsr::core::isDataTreeText(asText(binary)));
      REQUIRE(binary != buffer);
    }
  }

  WHEN("The string conveniences are used")
  {
    vsr::core::DataTree destination;
    REQUIRE(destination.fromText(source.toText()));

    THEN("The trees agree, and re-saving is byte-identical")
    {
      REQUIRE(destination.root()["settings"]["sampleCount"].getValueAs<int>()
          == 16);
      REQUIRE(destination.toText() == source.toText());
    }
  }
}

SCENARIO("Reading text into a DataNode replaces what it held", "[DataTree]")
{
  GIVEN("A text subtree and a node that already has contents")
  {
    vsr::core::DataTree source;
    source.root()["fromFile"] = 1;
    const std::string text = source.toText();

    vsr::core::DataTree destination;
    destination.root()["target"]["keep"] = 2;
    destination.root()["target"]["alsoKeep"] = 3;

    WHEN("The text is read into that node")
    {
      REQUIRE(destination.root()["target"].fromText(text));

      THEN("What the node held is gone, not merged with")
      {
        REQUIRE(destination.root()["target"].child("keep") == nullptr);
        REQUIRE(destination.root()["target"].child("alsoKeep") == nullptr);
        REQUIRE(
            destination.root()["target"]["fromFile"].getValueAs<int>() == 1);
        REQUIRE(destination.root()["target"].numChildren() == 1);
      }
    }
  }

  GIVEN("A node holding a value rather than children")
  {
    vsr::core::DataTree source;
    source.root()["fromFile"] = 1;

    vsr::core::DataTree destination;
    destination.root()["target"] = 99;

    WHEN("Text is read into it")
    {
      REQUIRE(destination.root()["target"].fromText(source.toText()));

      THEN("Its value is gone too: the node holds exactly what was read")
      {
        REQUIRE(destination.root()["target"].empty());
        REQUIRE(
            destination.root()["target"]["fromFile"].getValueAs<int>() == 1);
      }
    }
  }
}

SCENARIO("A failed text read leaves the node empty", "[DataTree]")
{
  GIVEN("Malformed text and a populated node")
  {
    const std::string text = "vsr-text 1\nvalues {\n  a = int32 1\n";

    vsr::core::DataTree destination;
    destination.root()["target"]["keep"] = 1;

    WHEN("The text is read into the node")
    {
      WarningRecorder recorder;
      const bool ok = destination.root()["target"].fromText(text);

      THEN("The read reports failure, naming the line and column")
      {
        REQUIRE_FALSE(ok);
        REQUIRE(recorder.namesLineAndColumn());
      }

      THEN("No half-built subtree is left behind")
      {
        REQUIRE(destination.root()["target"].numChildren() == 0);
        REQUIRE(destination.root()["target"].empty());
      }
    }

    WHEN("An observed node fails to read")
    {
      RecordingObserver observer;
      destination.setObserver(&observer);

      REQUIRE_FALSE(destination.root()["target"].fromText(text));

      THEN("One subtree-replaced signal still arrives: the node did change")
      {
        REQUIRE(observer.signals.size() == 1);
        REQUIRE(observer.signals[0].kind == RecordedSignal::SubtreeReplaced);
        REQUIRE(observer.signals[0].path == "/target");
      }
    }
  }
}

SCENARIO(
    "vsr::core::DataTree collapses a text read to one signal", "[DataTree]")
{
  GIVEN("The text of a tree of many nodes")
  {
    vsr::core::DataTree source;
    for (int i = 0; i < 32; i++)
      source.root()["values"][std::to_string(i)] = i;

    std::vector<std::byte> buffer;
    REQUIRE(source.write(buffer, vsr::core::Encoding::Text));

    WHEN("It is read into an observed tree")
    {
      vsr::core::DataTree destination;
      RecordingObserver observer;
      destination.setObserver(&observer);

      REQUIRE(destination.read(buffer));

      THEN("Only the subtree-replaced signal arrives, naming the root")
      {
        REQUIRE(observer.signals.size() == 1);
        REQUIRE(observer.signals[0].kind == RecordedSignal::SubtreeReplaced);
        REQUIRE(vsr::core::DataPath(observer.signals[0].path).isRoot());
      }

      THEN("The tree really was populated")
      {
        REQUIRE(destination.root()["values"].numChildren() == 32);
        REQUIRE(destination.root()["values"]["7"].getValueAs<int>() == 7);
      }
    }
  }
}

SCENARIO(
    "The Text Encoding sample is an executable specification", "[DataTree]")
{
  GIVEN("The hand-written sample in canonical form")
  {
    vsr::core::DataTree tree;
    REQUIRE(tree.fromText(TEXT_SAMPLE));
    auto &root = tree.root();

    THEN("Re-saving it is byte-identical")
    {
      REQUIRE(tree.toText() == TEXT_SAMPLE);
    }

    THEN("Every literal decoded to the value it spells")
    {
      REQUIRE(root["flags"]["enabled"].getValueAs<bool>() == true);
      REQUIRE(root["flags"]["hidden"].getValueAs<bool>() == false);
      REQUIRE(root["integers"]["i8"].getValueAs<int8_t>() == -128);
      REQUIRE(root["integers"]["u8"].getValueAs<uint8_t>() == 255);
      REQUIRE(root["integers"]["i64"].getValueAs<int64_t>() == INT64_MIN);
      REQUIRE(root["integers"]["u64"].getValueAs<uint64_t>() == UINT64_MAX);
      REQUIRE(root["integers"]["kind"].getValue().type() == ANARI_DATA_TYPE);
      REQUIRE(
          root["fixed"]["rgba"].getValue().type() == ANARI_UFIXED8_RGBA_SRGB);
      REQUIRE(root["floats"]["single"].getValueAs<float>() == 0.1f);
      REQUIRE(root["floats"]["precise"].getValueAs<double>() == 0.1);
      REQUIRE(std::isnan(root["floats"]["notANumber"].getValueAs<float>()));
      REQUIRE(std::isinf(root["floats"]["infinite"].getValueAs<double>()));
      REQUIRE(root["floats"]["negInfinite"].getValueAs<float>() < 0.f);
      REQUIRE(root["floats"]["half"].getValue().type() == ANARI_FLOAT16);
      REQUIRE(
          root["vectors"]["position"].getValue().type() == ANARI_FLOAT32_VEC3);
      REQUIRE(root["matrices"]["transform"].getValue().type()
          == ANARI_FLOAT32_MAT4);
      REQUIRE(root["strings"]["escaped"].getValueAs<std::string>()
          == "quote \" backslash \\ newline \n tab \t bell \x07");
      REQUIRE(root["strings"]["utf8"].getValueAs<std::string>() == "grüße");
      REQUIRE(root["strings"]["empty"].getValueAs<std::string>().empty());

      anari::DataType type = ANARI_UNKNOWN;
      size_t idx = 0;
      root["references"]["geometry"].getValueAsObjectIdx(&type, &idx);
      REQUIRE(type == ANARI_GEOMETRY);
      REQUIRE(idx == 12);

      const float *positions = nullptr;
      size_t numPositions = 0;
      REQUIRE(root["arrays"]["positions"].arrayType() == ANARI_FLOAT32_VEC3);
      const void *data = nullptr;
      root["arrays"]["positions"].getValueAsArray(&type, &data, &numPositions);
      positions = static_cast<const float *>(data);
      REQUIRE(numPositions == 3);
      REQUIRE(positions[3] == 1.f);

      root["arrays"]["references"].getValueAsArray(&type, &data, &idx);
      REQUIRE(type == ANARI_GEOMETRY);
      REQUIRE(idx == 2);
      REQUIRE(static_cast<const size_t *>(data)[1] == 2);

      REQUIRE(root["arrays"]["empty"].holdsArray());
      root["arrays"]["empty"].getValueAsArray(&type, &data, &idx);
      REQUIRE(type == ANARI_FLOAT32);
      REQUIRE(idx == 0);

      REQUIRE(root["keyed"]["2"].getValueAs<int>() == 2);
      REQUIRE(root["needs quoting"]["tab\tname"].getValueAs<int>() == 1);
      REQUIRE(root["needs quoting"]["-"].getValueAs<int>() == 3);
    }

    THEN("Marked entries are Anonymous Nodes, addressed by ordinal")
    {
      auto &sequence = root["sequence"];
      REQUIRE(sequence.numChildren() == 4);
      REQUIRE(sequence.child(0)->path().str() == "/sequence/[0]");
      REQUIRE(sequence.child(0)->getValueAs<float>() == 0.f);
      REQUIRE(sequence.child(1)->getValueAs<float>() == 1.f);
      REQUIRE((*sequence.child(1))["label"].getValueAs<std::string>()
          == "first keyframe");
      REQUIRE(sequence.child(2)->empty());
      REQUIRE(sequence.child(2)->numChildren() == 1);
      REQUIRE(sequence.child(3)->empty());
      REQUIRE(sequence.child(3)->isLeaf());
    }

    THEN("An interior node keeps its value and a value-less leaf is empty")
    {
      REQUIRE(root["interior"].getValueAs<int>() == 7);
      REQUIRE(root["interior"]["child"].getValueAs<int>() == 8);
      REQUIRE(root["valueless"].empty());
      REQUIRE(root["valueless"].isLeaf());
    }
  }

  GIVEN("The same sample with comments, free whitespace, and explicit types")
  {
    vsr::core::DataTree tree;
    REQUIRE(tree.fromText(TEXT_SAMPLE_COMMENTED));

    THEN("It saves as the canonical form, comments discarded")
    {
      REQUIRE(tree.toText() == TEXT_SAMPLE);
    }
  }
}

SCENARIO("The Text Encoding is strictly richer than binary", "[DataTree]")
{
  GIVEN("An interior node with a value, which only text can describe")
  {
    const std::string text =
        "vsr-text 1\ninterior = int32 7 {\n  child = int32 8\n}\n";
    vsr::core::DataTree tree;
    REQUIRE(tree.fromText(text));

    THEN("Text preserves it")
    {
      REQUIRE(tree.root()["interior"].getValueAs<int>() == 7);
      REQUIRE(tree.root()["interior"]["child"].getValueAs<int>() == 8);
      REQUIRE(tree.toText() == text);
    }

    THEN("Binary drops the interior value and keeps the child")
    {
      std::vector<std::byte> binary;
      REQUIRE(tree.write(binary));
      vsr::core::DataTree viaBinary;
      REQUIRE(viaBinary.read(binary));
      REQUIRE(viaBinary.root()["interior"].empty());
      REQUIRE(viaBinary.root()["interior"]["child"].getValueAs<int>() == 8);
      REQUIRE(viaBinary.toText() != text);
    }
  }

  GIVEN("A leaf, which roots an empty tree")
  {
    vsr::core::DataTree source;
    source.root()["count"] = 16;

    THEN("Its text is the header alone, and reads back as an empty tree")
    {
      const std::string text = source.root()["count"].toText();
      REQUIRE(text == "vsr-text 1\n");

      vsr::core::DataTree destination;
      destination.root()["stale"] = 1;
      REQUIRE(destination.fromText(text));
      REQUIRE(destination.root().numChildren() == 0);
    }
  }

  GIVEN("A node holding an External Array")
  {
    float values[] = {1.f, 2.f, 3.f};
    vsr::core::DataTree external;
    external.root()["data"].setValueAsExternalArray(ANARI_FLOAT32, values, 3);
    vsr::core::DataTree owned;
    owned.root()["data"].setValueAsArray(values, 3);

    THEN("It is written by value and reads back owned")
    {
      REQUIRE(external.toText() == owned.toText());
      REQUIRE(external.toText().find("float32[] [\n  1 2 3\n]")
          != std::string::npos);

      vsr::core::DataTree destination;
      REQUIRE(destination.fromText(external.toText()));
      REQUIRE(destination.root()["data"].holdsArray());
      REQUIRE_FALSE(destination.root()["data"].holdsExternalArray());
    }
  }

  GIVEN("A pointer-typed value")
  {
    vsr::core::DataTree tree;
    int target = 0;
    tree.root()["pointer"] =
        vsr::core::Any(ANARI_VOID_POINTER, static_cast<const void *>(&target));
    tree.root()["after"] = 1;

    THEN("It is written as a value-less node, with a warning")
    {
      WarningRecorder recorder;
      const std::string text = tree.toText();
      REQUIRE(recorder.warnings.size() == 1);
      REQUIRE(text == "vsr-text 1\npointer {}\nafter = int32 1\n");
    }
  }
}

SCENARIO("The encoding of a file is detected, not declared", "[DataTree]")
{
  GIVEN("A tree saved twice, once in each encoding, both as .vsr")
  {
    vsr::core::DataTree source;
    source.root()["camera"]["fovy"] = 0.75f;
    source.root()["name"] = "shot";

    const auto directory = std::filesystem::temp_directory_path();
    const auto binaryPath = (directory / "vsr_test_detect_binary.vsr").string();
    const auto textPath = (directory / "vsr_test_detect_text.vsr").string();
    REQUIRE(source.save(binaryPath.c_str()));
    REQUIRE(source.save(textPath.c_str(), vsr::core::Encoding::Text));

    THEN("The text file is text regardless of its extension")
    {
      REQUIRE(vsr::core::isDataTreeTextFile(textPath.c_str()));
      REQUIRE_FALSE(vsr::core::isDataTreeTextFile(binaryPath.c_str()));

      std::vector<std::byte> bytes(source.toText().size());
      std::FILE *file = std::fopen(textPath.c_str(), "rb");
      REQUIRE(file != nullptr);
      REQUIRE(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
      std::fclose(file);
      REQUIRE(asText(bytes) == source.toText());
    }

    THEN("load() takes no argument and accepts both")
    {
      vsr::core::DataTree fromBinary;
      REQUIRE(fromBinary.load(binaryPath.c_str()));
      REQUIRE(fromBinary.root()["camera"]["fovy"].getValueAs<float>() == 0.75f);

      vsr::core::DataTree fromText;
      REQUIRE(fromText.load(textPath.c_str()));
      REQUIRE(fromText.root()["camera"]["fovy"].getValueAs<float>() == 0.75f);
      REQUIRE(fromText.root()["name"].getValueAs<std::string>() == "shot");

      REQUIRE(fromBinary.toText() == fromText.toText());
    }

    std::filesystem::remove(binaryPath);
    std::filesystem::remove(textPath);
  }

  GIVEN("A detached DataNode")
  {
    vsr::core::DataNode detached;

    THEN("It writes the empty tree it roots and refuses to read text")
    {
      REQUIRE(detached.toText() == "vsr-text 1\n");
      REQUIRE_FALSE(detached.fromText("vsr-text 1\na = int32 1\n"));
    }
  }
}

SCENARIO("Every malformed Text Encoding input is a hard error", "[DataTree]")
{
  struct Malformed
  {
    const char *description;
    const char *text;
    const char *messageFragment;
  };

  // clang-format off
  const Malformed cases[] = {
      {"missing header", "a = int32 1\n", "missing header"},
      {"bad header", "vsr-text one\n", "malformed header"},
      {"header not alone on its line", "vsr-text 1 a = int32 1\n",
          "after the header"},
      {"unsupported header version", "vsr-text 2\n", "unsupported"},
      {"duplicate name in a scope",
          "vsr-text 1\ng {\n  a = int32 1\n  a = int32 2\n}\n", "duplicate"},
      {"unknown type", "vsr-text 1\na = float 1\n", "unknown type"},
      {"reserved anonymous-shaped bare name", "vsr-text 1\n<3> = int32 1\n",
          "synthesized anonymous"},
      {"reserved anonymous-shaped quoted name",
          "vsr-text 1\n\"<3>\" = int32 1\n", "synthesized anonymous"},
      {"empty quoted name", "vsr-text 1\n\"\" = int32 1\n", "empty"},
      {"integer out of range", "vsr-text 1\na = int8 200\n", "out of range"},
      {"negative unsigned integer", "vsr-text 1\na = uint32 -1\n",
          "out of range"},
      {"fractional integer", "vsr-text 1\na = int32 1.5\n", "out of range"},
      {"too few components", "vsr-text 1\na = float32_vec3 1 2\nb = int32 1\n",
          "component"},
      {"too many components", "vsr-text 1\na = float32 1 2\nb = int32 1\n",
          "component"},
      {"array count not a multiple of arity",
          "vsr-text 1\na = float32_vec2[] [ 1 2 3 ]\n", "multiple"},
      {"unterminated block", "vsr-text 1\na {\n  b = int32 1\n",
          "unterminated block"},
      {"unterminated string", "vsr-text 1\na = \"abc\n",
          "unterminated string"},
      {"unterminated array", "vsr-text 1\na = float32[] [ 1 2\n",
          "unterminated array"},
      {"marker outside a block", "vsr-text 1\na = -\n", "marker"},
      {"token after the final closing brace", "vsr-text 1\na {}\n}\n",
          "unexpected '}'"},
      {"stray bracket after the final closing brace", "vsr-text 1\na {}\n]\n",
          "expected a node name"},
      {"numeric literal without a type", "vsr-text 1\na = 1\n",
          "needs a type"},
      {"entry with neither value nor block",
          "vsr-text 1\na\nb = int32 1\n", "expected '=' or '{'"},
      {"pointer type in a value", "vsr-text 1\na = void_pointer 0\n",
          "no Text Encoding literal"},
      {"unknown escape", "vsr-text 1\na = \"\\q\"\n", "escape"},
      {"character outside the grammar", "vsr-text 1\na = int32 $1\n",
          "unexpected character"},
      {"bool spelled as a number", "vsr-text 1\na = bool 1\n",
          "'true' or 'false'"},
      {"object reference without an index", "vsr-text 1\na = geometry\n",
          "index"},
      {"object array element of another type",
          "vsr-text 1\na = geometry[] [ material@1 ]\n", "geometry@"},
  };
  // clang-format on

  for (const auto &c : cases) {
    INFO(c.description);

    vsr::core::DataTree destination;
    destination.root()["target"]["keep"] = 1;
    RecordingObserver observer;
    destination.setObserver(&observer);

    WarningRecorder recorder;
    const bool ok = destination.root()["target"].fromText(c.text);
    REQUIRE_FALSE(ok);

    REQUIRE(destination.root()["target"].numChildren() == 0);
    REQUIRE(destination.root()["target"].empty());

    REQUIRE(observer.signals.size() == 1);
    REQUIRE(observer.signals[0].kind == RecordedSignal::SubtreeReplaced);
    REQUIRE(observer.signals[0].path == "/target");

    REQUIRE(recorder.namesLineAndColumn());
    INFO(recorder.warnings[0]);
    REQUIRE(recorder.warnings[0].find(c.messageFragment) != std::string::npos);
  }
}

SCENARIO("Text Anonymous Nodes are minted like appended ones", "[DataTree]")
{
  GIVEN("Text with marked entries, at top level and in a block")
  {
    const std::string text =
        "vsr-text 1\n- = int32 10\n- = int32 11\nitems {\n  - = int32 20\n"
        "  - {\n    x = int32 21\n  }\n}\n";

    WHEN("It is read into a subtree")
    {
      vsr::core::DataTree tree;
      auto &subtree = tree.root()["subtree"];
      REQUIRE(subtree.fromText(text));

      THEN("The marked entries are anonymous and addressable by ordinal")
      {
        REQUIRE(subtree.numChildren() == 3);
        REQUIRE(subtree.child(0)->path().str() == "/subtree/[0]");
        REQUIRE(subtree.child(1)->path().str() == "/subtree/[1]");
        REQUIRE(subtree.child(1)->getValueAs<int>() == 11);
        REQUIRE(subtree.child(2)->path().str() == "/subtree/items");

        auto &items = subtree["items"];
        REQUIRE(items.numChildren() == 2);
        REQUIRE(items.child(1)->path().str() == "/subtree/items/[1]");
        REQUIRE((*items.child(1))["x"].getValueAs<int>() == 21);
        REQUIRE(tree.node(vsr::core::DataPath("/subtree/items/[0]"))
                    ->getValueAs<int>()
            == 20);
      }

      THEN("Their minted names are claimed against the process counter")
      {
        auto &items = subtree["items"];
        constexpr size_t NUM_APPENDS = 8;
        for (size_t i = 0; i < NUM_APPENDS; i++)
          items.append();
        REQUIRE(items.numChildren() == 2 + NUM_APPENDS);
      }

      THEN("The names never reach the text, only the markers do")
      {
        const std::string written = subtree.toText();
        REQUIRE(written == text);
        REQUIRE(written.find('<') == std::string::npos);
      }
    }
  }

  GIVEN("Anonymous nodes that came from binary")
  {
    vsr::core::DataTree source;
    auto &items = source.root()["items"];
    items.append() = 1;
    items.append()["nested"] = 2;

    std::vector<std::byte> binary;
    REQUIRE(source.write(binary));

    WHEN("The binary is converted to text and back")
    {
      vsr::core::DataTree viaBinary;
      REQUIRE(viaBinary.read(binary));
      vsr::core::DataTree viaText;
      REQUIRE(viaText.fromText(viaBinary.toText()));

      THEN("The sequence survives with its anonymity intact")
      {
        REQUIRE(viaText.root()["items"].numChildren() == 2);
        REQUIRE(viaText.root()["items"].child(0)->path().str() == "/items/[0]");
        REQUIRE((*viaText.root()["items"].child(1))["nested"].getValueAs<int>()
            == 2);
        REQUIRE(viaText.toText() == source.toText());
      }
    }
  }
}
