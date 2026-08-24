// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/io/archives/AnimationArchive.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cstdio>
#include <filesystem>

SCENARIO("Animation Archives add only when Scene bindings are compatible",
    "[AnimationArchive]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager source(&scene);
  auto geometry = scene.createObject<vsr::scene::Geometry>("sphere");
  const float times[] = {0.f, 1.f};
  const float values[] = {1.f, 2.f};
  auto &animation = source.addAnimation("radius");
  animation.addObjectParameterBinding(
      geometry.data(), "radius", ANARI_FLOAT32, values, times, 2);

  vsr::core::DataTree tree;
  REQUIRE(vsr::io::serialize_AnimationArchive(animation, tree.root()));

  vsr::animation::AnimationManager target(&scene);
  auto *restored = vsr::io::deserialize_AnimationArchive(target, tree.root());
  REQUIRE(restored != nullptr);
  REQUIRE(restored->name() == "radius");
  REQUIRE(restored->objectParameterBindings().size() == 1);
  REQUIRE(
      restored->objectParameterBindings().front().target() == geometry.data());

  vsr::core::DataTree invalidTree;
  invalidTree.root() = tree.root();
  (*invalidTree.root()["objectBindings"].child(0))["targetIndex"] = size_t(99);
  const auto countBefore = target.animations().size();
  REQUIRE(vsr::io::deserialize_AnimationArchive(target, invalidTree.root())
      == nullptr);
  REQUIRE(target.animations().size() == countBefore);

  vsr::core::DataTree malformedTree;
  malformedTree.root() = tree.root();
  (*malformedTree.root()["objectBindings"].child(0))["targetIndex"] =
      "not an index";
  REQUIRE(vsr::io::deserialize_AnimationArchive(target, malformedTree.root())
      == nullptr);
  REQUIRE(target.animations().size() == countBefore);

  const auto filename =
      (std::filesystem::temp_directory_path() / "vsr_animation_archive.vsr")
          .string();
  REQUIRE(vsr::io::save_AnimationArchive(animation, filename.c_str()));
  vsr::animation::AnimationManager fileTarget(&scene);
  REQUIRE(vsr::io::load_AnimationArchive(fileTarget, filename.c_str()));
  std::remove(filename.c_str());
}
