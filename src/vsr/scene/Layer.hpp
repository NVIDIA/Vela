// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/scene/LayerNodeData.hpp"

namespace vsr::scene {

struct Scene;

/*
 * Named Forest-based hierarchy of LayerNodeData nodes owned by a Scene;
 * nodes carry transforms or Object references and can be traversed, queried
 * for ancestry, and toggled active per-layer.
 *
 * Example:
 *   auto *layer = scene.addLayer("main");
 *   auto child = scene.insertChildObjectNode(layer->root(), surfaceRef);
 *   layer->traverse(layer->root(), myVisitor);
 */
struct Layer final
{
  using Visitor = ForestVisitor<LayerNodeData>;
  using VisitorEntryFunction = ForestVisitorEntryFunction<LayerNodeData>;
  using VisitorExitFunction = ForestVisitorExitFunction<LayerNodeData>;
  using ConstVisitorEntryFunction =
      ConstForestVisitorEntryFunction<LayerNodeData>;
  using ConstVisitorExitFunction =
      ConstForestVisitorExitFunction<LayerNodeData>;

  Layer(Scene *scene, std::string name = "<unnamed layer>");
  ~Layer();

  Scene *scene() const;

  const std::string &name() const;
  std::string &editableName();

  // Nodes within the layer //

  size_t size() const;
  size_t capacity() const;
  bool empty() const;

  LayerNodeRef root() const;
  LayerNodeRef at(size_t i) const;
  void erase(LayerNodeRef obj);
  void clear();

  bool isAncestorOf(LayerNodeRef potentialAncestor, LayerNodeRef node) const;

  void traverse(LayerNodeRef start, Visitor &visitor);
  void traverse(LayerNodeRef start, VisitorEntryFunction &&f);
  void traverse(LayerNodeRef start,
      VisitorEntryFunction &&onNodeEntry,
      VisitorExitFunction &&onNodeExit);

  void traverse_const(LayerNodeRef start, Visitor &visitor) const;
  void traverse_const(LayerNodeRef start, ConstVisitorEntryFunction &&f) const;
  void traverse_const(LayerNodeRef start,
      ConstVisitorEntryFunction &&onNodeEntry,
      ConstVisitorExitFunction &&onNodeExit) const;

 private:
  Scene *m_scene{nullptr};
  std::string m_name;
  Forest<LayerNodeData> m_tree{{this, math::IDENTITY_MAT4, "root"}};
};

using LayerVisitor = Layer::Visitor;

} // namespace vsr::scene
