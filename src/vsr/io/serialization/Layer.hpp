// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::io {

/*
 * How a serialized layer numbers its nodes when it is read back.
 *
 * Archive: nodes are written in traversal order and deserialize_Layer hands
 * them fresh, dense indices; the on-disk Scene Archive form.
 *
 * Preserved: each node also records its forest index and deserialize_Layer
 * places it back in that slot, so a layer rebuilt elsewhere (a client's
 * Structural Mirror) names every node by the same index as the source.
 *
 * Example:
 *   serialize_Layer(*layer, node, LayerNodeNumbering::Preserved);
 *   deserialize_Layer(node, *mirrorLayer, mirrorScene);
 *   mirrorLayer->at(serverNode.index()); // the same node
 */
enum class LayerNodeNumbering
{
  Archive,
  Preserved
};

void serialize_Layer(const scene::Layer &layer,
    core::DataNode &node,
    LayerNodeNumbering numbering = LayerNodeNumbering::Archive);
void serialize_LayerSubtree(
    const scene::Layer &layer, scene::LayerNodeRef start, core::DataNode &node);
// Clears the layer, then rebuilds it: a node that records its index is
// placed back in that slot; any other node, or one whose slot is taken, is
// appended with the next dense index.
void deserialize_Layer(
    core::DataNode &node, scene::Layer &layer, scene::Scene &scene);

} // namespace vsr::io
