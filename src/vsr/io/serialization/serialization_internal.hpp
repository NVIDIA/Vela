// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_io
#include "vsr/io/archives/ArchiveValidation.hpp"
#include "vsr/io/archives/SubtreeArchiveContent.hpp"
#include "vsr/io/serialization.hpp"
// vsr_scene
#include "vsr/scene/Scene.hpp"
// std
#include <string_view>
#include <vector>

namespace vsr::animation {
struct Animation;
struct AnimationManager;
} // namespace vsr::animation

namespace vsr::io {

using namespace vsr::scene;

namespace schema {

inline constexpr std::string_view SCENE_FULL = "vsr.scene.full";
inline constexpr std::string_view SCENE_CAMERAS_AND_RENDERERS =
    "vsr.scene.cameras-and-renderers";
inline constexpr std::string_view SCENE_CAMERAS = "vsr.scene.cameras";
inline constexpr std::string_view SCENE_RENDERERS = "vsr.scene.renderers";
inline constexpr std::string_view OBJECT_SURFACE = "vsr.object.surface";
inline constexpr std::string_view OBJECT_VOLUME = "vsr.object.volume";
inline constexpr std::string_view LAYER_SUBTREE = "vsr.layer.subtree";

} // namespace schema

namespace detail {

void serialize_LayerSubtree(const scene::Layer &layer,
    scene::LayerNodeRef start,
    core::DataNode &node,
    const std::vector<scene::LayerNodeRef> *excluded);

enum class LegacyExcludedAnimationPolicy
{
  Retain,
  OmitOwned
};

struct LegacySceneExclusion
{
  std::vector<scene::LayerNodeRef> roots;
  ArchiveObjectPolicy objectPolicy{ArchiveObjectPolicy::LightsOnly};
  LegacyExcludedAnimationPolicy animations{
      LegacyExcludedAnimationPolicy::Retain};
};

struct LegacySceneSerializationOptions
{
  bool forceProxyArrays{false};
  animation::AnimationManager *animationManager{nullptr};
  LegacySceneExclusion exclusion;
};

void serializeLegacyScenePayload(scene::Scene &scene,
    core::DataNode &root,
    const LegacySceneSerializationOptions &options = {});
ArchiveValidationResult validateLegacyScenePayload(core::DataNode &root);
bool tryDeserializeLegacyScenePayload(scene::Scene &scene,
    core::DataNode &root,
    ArchiveValidationResult *result = nullptr,
    animation::AnimationManager *animationManager = nullptr);

void serializeLegacyCameraRendererPayload(
    scene::Scene &scene, core::DataNode &root);
ArchiveValidationResult validateLegacyCameraRendererPayload(
    core::DataNode &root);
bool tryDeserializeLegacyCameraRendererPayload(scene::Scene &scene,
    core::DataNode &root,
    ArchiveValidationResult *result = nullptr);

} // namespace detail

// clang-format off

// Animations //

void animationToNode(const animation::Animation &anim, core::DataNode &node);
void nodeToAnimation(core::DataNode &node, animation::Animation &anim, Scene &scene);
void animationManagerToNode(const animation::AnimationManager &mgr, core::DataNode &node);
void nodeToAnimationManager(core::DataNode &node, animation::AnimationManager &mgr, Scene &scene);

// clang-format on

} // namespace vsr::io
