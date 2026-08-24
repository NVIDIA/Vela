// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// catch
#include "catch.hpp"
// vsr
#include "vsr/animation/Animation.hpp"
#include "vsr/animation/AnimationManager.hpp"
#include "vsr/core/DataTree.hpp"
#include "vsr/io/archives/AnimationManagerArchive.hpp"
#include "vsr/io/archives/SceneArchive.hpp"
#include "vsr/scene/Scene.hpp"
// std
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <vector>

SCENARIO(
    "Scene Archives serialize sparse pools observationally", "[SceneArchive]")
{
  vsr::scene::Scene source;
  auto removed = source.createObject<vsr::scene::Geometry>("sphere");
  auto retained = source.createObject<vsr::scene::Geometry>("cylinder");
  retained->setName("retained geometry");
  source.removeObject(removed.data());
  source.insertChildObjectNode(source.defaultLayer()->root(), retained);
  auto objectArray = source.createArray(ANARI_GEOMETRY, 1);
  objectArray->setData(std::vector<size_t>{retained.index()});

  REQUIRE(retained.index() == 1);
  REQUIRE_FALSE(source.objectDB().geometry.is_dense());

  vsr::core::DataTree tree;
  REQUIRE(vsr::io::serialize_SceneArchive(source, tree.root()));

  REQUIRE(retained.index() == 1);
  REQUIRE_FALSE(source.objectDB().geometry.is_dense());
  auto *geometry = tree.root()["objectDB"]["geometry"].child(0);
  REQUIRE(geometry != nullptr);
  REQUIRE((*geometry)["self"].getValue().getAsObjectIndex() == 0);

  vsr::scene::Scene target;
  REQUIRE(vsr::io::deserialize_SceneArchive(target, tree.root()));
  REQUIRE(target.numberOfObjects(ANARI_GEOMETRY) == 1);
  auto restored = target.getObject<vsr::scene::Geometry>(0);
  REQUIRE(restored);
  REQUIRE(restored->name() == "retained geometry");
  auto restoredArray = target.getObject<vsr::scene::Array>(0);
  REQUIRE(restoredArray);
  REQUIRE(restoredArray->dataAs<size_t>()[0] == 0);

  auto *layer = target.defaultLayer();
  auto child = layer->root()->next();
  REQUIRE(child);
  REQUIRE((*child)->getObject() == restored.data());

  const auto filename =
      (std::filesystem::temp_directory_path() / "vsr_scene_archive.vsr")
          .string();
  REQUIRE(vsr::io::save_SceneArchive(source, filename.c_str()));
  vsr::scene::Scene fileTarget;
  REQUIRE(vsr::io::load_SceneArchive(fileTarget, filename.c_str()));
  REQUIRE(fileTarget.numberOfObjects(ANARI_GEOMETRY) == 1);
  std::remove(filename.c_str());

  vsr::core::DataTree invalidTree;
  invalidTree.root() = tree.root();
  (*invalidTree.root()["objectDB"]["geometry"].child(0))["self"] =
      vsr::core::Any(ANARI_GEOMETRY, size_t(7));
  vsr::scene::Scene preservedTarget;
  preservedTarget.createObject<vsr::scene::Geometry>("cone");
  preservedTarget.addLayer("preserved");
  const auto geometriesBefore = preservedTarget.numberOfObjects(ANARI_GEOMETRY);
  REQUIRE_FALSE(
      vsr::io::deserialize_SceneArchive(preservedTarget, invalidTree.root()));
  REQUIRE(preservedTarget.numberOfObjects(ANARI_GEOMETRY) == geometriesBefore);
  REQUIRE(preservedTarget.layer("preserved") != nullptr);
}

SCENARIO(
    "Scene Archives support full and proxy array carriers", "[SceneArchive]")
{
  vsr::scene::Scene source;
  auto array = source.createArray(ANARI_FLOAT32, 3);
  array->setData(std::vector<float>{1.f, 2.f, 3.f});

  vsr::core::DataTree fullTree;
  REQUIRE(vsr::io::serialize_SceneArchive(
      source, fullTree.root(), vsr::io::ArrayDataPolicy::IncludeData));
  std::vector<std::byte> buffer;
  REQUIRE(fullTree.write(buffer));
  vsr::core::DataTree receivedTree;
  REQUIRE(receivedTree.read(buffer));
  vsr::scene::Scene fullTarget;
  REQUIRE(vsr::io::deserialize_SceneArchive(fullTarget, receivedTree.root()));
  auto fullArray = fullTarget.getObject<vsr::scene::Array>(0);
  REQUIRE(fullArray);
  REQUIRE_FALSE(fullArray->isProxy());
  REQUIRE(fullArray->dataAs<float>()[2] == 3.f);

  vsr::core::DataTree proxyTree;
  REQUIRE(vsr::io::serialize_SceneArchive(
      source, proxyTree.root(), vsr::io::ArrayDataPolicy::ProxyOnly));
  vsr::scene::Scene proxyTarget;
  REQUIRE(vsr::io::deserialize_SceneArchive(proxyTarget, proxyTree.root()));
  auto proxyArray = proxyTarget.getObject<vsr::scene::Array>(0);
  REQUIRE(proxyArray);
  REQUIRE(proxyArray->isProxy());
  REQUIRE(proxyArray->size() == 3);
}

SCENARIO("Scene and Animation Manager Archives share dense mappings",
    "[SceneArchive]")
{
  vsr::scene::Scene source;
  vsr::animation::AnimationManager sourceAnimations(&source);

  auto removedGeometry = source.createObject<vsr::scene::Geometry>("sphere");
  auto retainedGeometry = source.createObject<vsr::scene::Geometry>("cylinder");
  retainedGeometry->setName("retained geometry");
  source.removeObject(removedGeometry.data());
  REQUIRE(retainedGeometry.index() == 1);

  auto removedMaterial = source.createObject<vsr::scene::Material>("matte");
  auto retainedMaterial = source.createObject<vsr::scene::Material>("matte");
  retainedMaterial->setName("retained material");
  source.removeObject(removedMaterial.data());
  REQUIRE(retainedMaterial.index() == 2);

  auto removedTransform = source.insertChildTransformNode(
      source.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "removed");
  auto retainedTransform = source.insertChildTransformNode(
      source.defaultLayer()->root(), vsr::math::IDENTITY_MAT4, "retained");
  source.removeNode(removedTransform);
  REQUIRE(retainedTransform.index() == 2);

  const float times[] = {0.f, 1.f};
  vsr::scene::Object *keyframes[] = {
      retainedMaterial.data(), retainedMaterial.data()};
  auto &animation = sourceAnimations.addAnimation("sparse animation");
  animation.addObjectParameterBinding(
      retainedGeometry.data(), "material", ANARI_MATERIAL, keyframes, times, 2);
  animation.addTransformBinding(retainedTransform);

  vsr::core::DataTree sceneTree;
  vsr::core::DataTree animationTree;
  REQUIRE(vsr::io::serialize_SceneAndAnimationManagerArchives(
      source, sourceAnimations, sceneTree.root(), animationTree.root()));

  auto *serializedAnimation = animationTree.root()["objects"].child(0);
  REQUIRE(serializedAnimation != nullptr);
  auto *objectBinding = (*serializedAnimation)["objectBindings"].child(0);
  REQUIRE(objectBinding != nullptr);
  REQUIRE((*objectBinding)["targetIndex"].getValueAs<size_t>() == 0);
  const void *serializedKeyframes = nullptr;
  size_t numSerializedKeyframes = 0;
  anari::DataType serializedKeyframeType = ANARI_UNKNOWN;
  (*objectBinding)["data"].getValueAsArray(
      &serializedKeyframeType, &serializedKeyframes, &numSerializedKeyframes);
  REQUIRE(serializedKeyframeType == ANARI_MATERIAL);
  REQUIRE(numSerializedKeyframes == 2);
  REQUIRE(static_cast<const size_t *>(serializedKeyframes)[0] == 1);

  auto *transformBinding = (*serializedAnimation)["transformBindings"].child(0);
  REQUIRE(transformBinding != nullptr);
  REQUIRE((*transformBinding)["nodeIndex"].getValueAs<size_t>() == 1);

  vsr::scene::Scene target;
  vsr::animation::AnimationManager targetAnimations(&target);
  REQUIRE(vsr::io::deserialize_SceneArchive(target, sceneTree.root()));
  REQUIRE(vsr::io::deserialize_AnimationManagerArchive(
      targetAnimations, animationTree.root()));

  auto restoredGeometry = target.getObject<vsr::scene::Geometry>(0);
  auto restoredMaterial = target.getObject<vsr::scene::Material>(1);
  REQUIRE(restoredGeometry);
  REQUIRE(restoredMaterial);
  REQUIRE(restoredGeometry->name() == "retained geometry");
  REQUIRE(restoredMaterial->name() == "retained material");
  REQUIRE(targetAnimations.animations().size() == 1);
  const auto &restoredAnimation = targetAnimations.animations().front();
  REQUIRE(restoredAnimation.objectParameterBindings().front().target()
      == restoredGeometry.data());
  const auto *restoredKeyframes = static_cast<const size_t *>(
      restoredAnimation.objectParameterBindings().front().data().data());
  REQUIRE(restoredKeyframes[0] == restoredMaterial.index());
  REQUIRE((*restoredAnimation.transformBindings().front().target())->name()
      == "retained");
}
