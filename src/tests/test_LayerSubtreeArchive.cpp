// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/DataTree.hpp"
#include "vsr/io/archives/LayerSubtreeArchive.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cstdio>
#include <filesystem>

SCENARIO("Layer Subtree Archives require a destination and add atomically",
    "[LayerSubtreeArchive]")
{
  vsr::scene::Scene source;
  auto geometry = source.createObject<vsr::scene::Geometry>("sphere");
  auto material = source.createObject<vsr::scene::Material>("matte");
  auto surface = source.createSurface("surface", geometry, material);
  auto subtree =
      source.insertChildNode(source.defaultLayer()->root(), "subtree");
  source.insertChildObjectNode(subtree, surface, "instance");

  vsr::core::DataTree tree;
  REQUIRE(vsr::io::serialize_LayerSubtreeArchive(subtree, tree.root()));
  REQUIRE(tree.root().child("animations") == nullptr);

  vsr::scene::Scene target;
  const auto surfacesBefore = target.numberOfObjects(ANARI_SURFACE);
  REQUIRE_FALSE(vsr::io::deserialize_LayerSubtreeArchive({}, tree.root()));
  REQUIRE(target.numberOfObjects(ANARI_SURFACE) == surfacesBefore);

  auto restored = vsr::io::deserialize_LayerSubtreeArchive(
      target.defaultLayer()->root(), tree.root());
  REQUIRE(restored);
  REQUIRE((*restored)->name() == "subtree");
  REQUIRE(target.numberOfObjects(ANARI_SURFACE) == surfacesBefore + 1);

  vsr::core::DataTree invalidTree;
  invalidTree.root() = tree.root();
  invalidTree.root()["subtree"]["value"] =
      vsr::core::Any(ANARI_SURFACE, size_t(99));
  const auto objectsBeforeFailure = target.numberOfObjects(ANARI_SURFACE);
  REQUIRE_FALSE(vsr::io::deserialize_LayerSubtreeArchive(
      target.defaultLayer()->root(), invalidTree.root()));
  REQUIRE(target.numberOfObjects(ANARI_SURFACE) == objectsBeforeFailure);

  const auto filename =
      (std::filesystem::temp_directory_path() / "vsr_layer_subtree_archive.vsr")
          .string();
  REQUIRE(vsr::io::save_LayerSubtreeArchive(subtree, filename.c_str()));
  vsr::scene::Scene fileTarget;
  REQUIRE(vsr::io::load_LayerSubtreeArchive(
      fileTarget.defaultLayer()->root(), filename.c_str()));
  std::remove(filename.c_str());

  vsr::core::DataTree legacyTree;
  legacyTree.root() = tree.root();
  legacyTree.root().remove("__vsr_metadata");
  vsr::scene::Scene legacyTarget;
  REQUIRE(vsr::io::deserialize_LayerSubtreeArchive(
      legacyTarget.defaultLayer()->root(), legacyTree.root()));
}
