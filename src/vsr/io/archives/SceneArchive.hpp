// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_io
#include "vsr/io/archives/ArchiveValidation.hpp"

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::animation {
struct AnimationManager;
} // namespace vsr::animation

namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::io {

enum class ArrayDataPolicy
{
  IncludeData,
  ProxyOnly
};

bool serialize_SceneArchive(const scene::Scene &scene,
    core::DataNode &archive,
    ArrayDataPolicy arrayData = ArrayDataPolicy::IncludeData);

/* Serialize a Scene and its Animation Manager as a compatible Archive pair.
 * The manager must belong to the Scene; object and layer bindings in its
 * Archive use the same dense indices as the emitted Scene Archive. */
bool serialize_SceneAndAnimationManagerArchives(const scene::Scene &scene,
    const animation::AnimationManager &animationManager,
    core::DataNode &sceneArchive,
    core::DataNode &animationManagerArchive,
    ArrayDataPolicy arrayData = ArrayDataPolicy::IncludeData);
ArchiveValidationResult validate_SceneArchive(core::DataNode &archive);
bool deserialize_SceneArchive(scene::Scene &scene,
    core::DataNode &archive,
    ArchiveValidationResult *validation = nullptr);

bool save_SceneArchive(const scene::Scene &scene,
    const char *filename,
    ArrayDataPolicy arrayData = ArrayDataPolicy::IncludeData);
bool load_SceneArchive(scene::Scene &scene,
    const char *filename,
    ArchiveValidationResult *validation = nullptr);

} // namespace vsr::io
