// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LegacyApplicationContext.h"

#include "Context.h"
// vsr_core
#include "vsr/core/DataTree.hpp"
// vsr_io
#include "vsr/io/archives/AnimationManagerArchive.hpp"
#include "vsr/io/archives/SceneArchive.hpp"

namespace vsr::app::detail {

namespace {

void copyArchiveContents(
    core::DataNode &destination, const core::DataNode &source)
{
  destination.reset();
  for (size_t i = 0; i < source.numChildren(); ++i) {
    if (auto *child = source.child(i))
      destination.append(child->name()) = *child;
  }
}

} // namespace

void serializeLegacyApplicationContext(Context &context, core::DataNode &node)
{
  core::DataTree animationManagerArchive;
  if (io::serialize_SceneAndAnimationManagerArchives(context.vsr.scene,
          context.vsr.animationMgr,
          node,
          animationManagerArchive.root())) {
    copyArchiveContents(node["animations"], animationManagerArchive.root());
  }
}

bool deserializeLegacyApplicationContext(Context &context, core::DataNode &node)
{
  return deserializeLegacySceneState(
      context.vsr.scene, context.vsr.animationMgr, node);
}

bool deserializeLegacySceneState(scene::Scene &scene,
    animation::AnimationManager &animationManager,
    core::DataNode &node)
{
  auto *context = node.child("context");
  auto &payload = context ? *context : node;
  if (!io::deserialize_SceneArchive(scene, payload))
    return false;
  if (auto *animations = payload.child("animations")) {
    return io::deserialize_AnimationManagerArchive(
        animationManager, *animations);
  }
  return true;
}

} // namespace vsr::app::detail
