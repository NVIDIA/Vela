// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/io/archives/AnimationManagerArchive.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cstdio>
#include <filesystem>

SCENARIO(
    "Animation Manager Archives replace validated state and restore stopped",
    "[AnimationManagerArchive]")
{
  vsr::scene::Scene scene;
  vsr::animation::AnimationManager source(&scene);
  auto geometry = scene.createObject<vsr::scene::Geometry>("sphere");
  source.setAnimationTime(0.25f);
  source.setAnimationIncrement(0.125f);
  source.setAnimationTotalFrames(41);
  source.setAnimationFPS(24.f);
  source.setLoop(false);
  source.play();
  const float times[] = {0.f, 1.f};
  const float values[] = {1.f, 2.f};
  source.addAnimation("archived animation")
      .addObjectParameterBinding(
          geometry.data(), "radius", ANARI_FLOAT32, values, times, 2);

  vsr::core::DataTree tree;
  REQUIRE(vsr::io::serialize_AnimationManagerArchive(source, tree.root()));

  vsr::animation::AnimationManager target(&scene);
  target.addAnimation("stale animation");
  target.play();
  REQUIRE(vsr::io::deserialize_AnimationManagerArchive(target, tree.root()));
  REQUIRE(target.animations().size() == 1);
  REQUIRE(target.animations().front().name() == "archived animation");
  REQUIRE(target.getAnimationTime() == 0.25f);
  REQUIRE(target.getAnimationIncrement() == 0.125f);
  REQUIRE(target.getAnimationTotalFrames() == 41);
  REQUIRE(target.getAnimationFPS() == 24.f);
  REQUIRE_FALSE(target.isLoop());
  REQUIRE_FALSE(target.isPlaying());
  REQUIRE(target.animations().front().objectParameterBindings().front().target()
      == geometry.data());

  const auto filename = (std::filesystem::temp_directory_path()
      / "vsr_animation_manager_archive.vsr")
                            .string();
  REQUIRE(vsr::io::save_AnimationManagerArchive(source, filename.c_str()));
  vsr::animation::AnimationManager fileTarget(&scene);
  REQUIRE(vsr::io::load_AnimationManagerArchive(fileTarget, filename.c_str()));
  REQUIRE(fileTarget.animations().size() == 1);
  std::remove(filename.c_str());

  vsr::core::DataTree invalidTree;
  invalidTree.root() = tree.root();
  invalidTree.root().remove("fps");
  vsr::animation::AnimationManager preserved(&scene);
  preserved.addAnimation("preserved animation");
  REQUIRE_FALSE(vsr::io::deserialize_AnimationManagerArchive(
      preserved, invalidTree.root()));
  REQUIRE(preserved.animations().size() == 1);
  REQUIRE(preserved.animations().front().name() == "preserved animation");

  vsr::scene::Scene incompatibleScene;
  vsr::animation::AnimationManager incompatible(&incompatibleScene);
  incompatible.addAnimation("also preserved");
  REQUIRE_FALSE(
      vsr::io::deserialize_AnimationManagerArchive(incompatible, tree.root()));
  REQUIRE(incompatible.animations().size() == 1);
  REQUIRE(incompatible.animations().front().name() == "also preserved");
}
