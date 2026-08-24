// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/core/DataTree.hpp"
#include "vsr/io/archives/ObjectArchive.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cstdio>
#include <filesystem>

SCENARIO(
    "Object Archives add a dependency closure atomically", "[ObjectArchive]")
{
  vsr::scene::Scene source;
  auto geometry = source.createObject<vsr::scene::Geometry>("sphere");
  geometry->setName("archived geometry");
  auto material = source.createObject<vsr::scene::Material>("matte");
  auto surface = source.createSurface("archived surface", geometry, material);

  vsr::core::DataTree tree;
  REQUIRE(vsr::io::serialize_ObjectArchive(*surface, tree.root()));

  vsr::scene::Scene target;
  const auto geometryCount = target.numberOfObjects(ANARI_GEOMETRY);
  auto *restored = vsr::io::deserialize_ObjectArchive(target, tree.root());
  REQUIRE(restored != nullptr);
  REQUIRE(restored->type() == ANARI_SURFACE);
  REQUIRE(restored->name() == "archived surface");
  REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == geometryCount + 1);

  vsr::core::DataTree invalidTree;
  invalidTree.root() = tree.root();
  auto &invalidRoot = invalidTree.root();
  auto *geometryNode = invalidRoot["objectDB"]["geometry"].child(0);
  REQUIRE(geometryNode != nullptr);
  (*geometryNode)["parameters"]["missing"]["value"] =
      vsr::core::Any(ANARI_ARRAY1D, size_t(99));

  const auto arraysBefore = target.numberOfObjects(ANARI_ARRAY);
  const auto geometriesBefore = target.numberOfObjects(ANARI_GEOMETRY);
  const auto materialsBefore = target.numberOfObjects(ANARI_MATERIAL);
  const auto surfacesBefore = target.numberOfObjects(ANARI_SURFACE);
  REQUIRE(vsr::io::deserialize_ObjectArchive(target, invalidRoot) == nullptr);
  REQUIRE(target.numberOfObjects(ANARI_ARRAY) == arraysBefore);
  REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == geometriesBefore);
  REQUIRE(target.numberOfObjects(ANARI_MATERIAL) == materialsBefore);
  REQUIRE(target.numberOfObjects(ANARI_SURFACE) == surfacesBefore);

  const auto filename =
      (std::filesystem::temp_directory_path() / "vsr_object_archive.vsr")
          .string();
  REQUIRE(vsr::io::save_ObjectArchive(*surface, filename.c_str()));
  vsr::scene::Scene fileTarget;
  REQUIRE(vsr::io::load_ObjectArchive(fileTarget, filename.c_str()));
  std::remove(filename.c_str());

  vsr::core::DataTree legacyTree;
  legacyTree.root() = tree.root();
  legacyTree.root().remove("__vsr_metadata");
  vsr::scene::Scene legacyTarget;
  REQUIRE(vsr::io::deserialize_ObjectArchive(legacyTarget, legacyTree.root()));
}
