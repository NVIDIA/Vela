// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/io/archives/LayerSubtreeArchive.hpp"
// vsr_core
#include "vsr/core/DataTree.hpp"
#include "vsr/core/Logging.hpp"
// vsr_io
#include "vsr/io/serialization/serialization_internal.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"

namespace vsr::io {

namespace {

const SubtreeArchiveContentDesc LAYER_SUBTREE_DESC = {
    "layer-subtree", schema::LAYER_SUBTREE, ArchiveObjectPolicy::All};

} // namespace

bool serialize_LayerSubtreeArchive(
    scene::LayerNodeRef subtree, core::DataNode &archive)
{
  return serialize_SubtreeArchiveContent(subtree, archive, LAYER_SUBTREE_DESC);
}

ArchiveValidationResult validate_LayerSubtreeArchive(core::DataNode &archive)
{
  auto result = validate_SubtreeArchiveContent(archive, LAYER_SUBTREE_DESC);
  if (result.accepted() && archive.child("animations")) {
    result.status = ArchiveValidationStatus::IncompatibleSchema;
    result.message = "Layer Subtree Archives cannot contain animations";
  }
  return result;
}

scene::LayerNodeRef deserialize_LayerSubtreeArchive(
    scene::LayerNodeRef destination, core::DataNode &archive)
{
  if (!destination) {
    core::logError(
        "[deserialize_LayerSubtreeArchive] destination node is invalid");
    return {};
  }

  auto *layer = (*destination).value().layer();
  auto *scene = layer ? layer->scene() : nullptr;
  if (!scene) {
    core::logError(
        "[deserialize_LayerSubtreeArchive] destination has no owning Scene");
    return {};
  }

  const auto validation = validate_LayerSubtreeArchive(archive);
  if (!validation.accepted()) {
    core::logError(
        "[deserialize_LayerSubtreeArchive] %s", validation.message.c_str());
    return {};
  }

  return deserialize_SubtreeArchiveContent(
      *scene, archive, destination, LAYER_SUBTREE_DESC)
      .root;
}

bool save_LayerSubtreeArchive(scene::LayerNodeRef subtree, const char *filename)
{
  if (!filename)
    return false;
  core::DataTree tree;
  return serialize_LayerSubtreeArchive(subtree, tree.root())
      && tree.save(filename);
}

scene::LayerNodeRef load_LayerSubtreeArchive(
    scene::LayerNodeRef destination, const char *filename)
{
  if (!destination || !filename)
    return {};
  core::DataTree tree;
  if (!tree.load(filename))
    return {};
  return deserialize_LayerSubtreeArchive(destination, tree.root());
}

} // namespace vsr::io
