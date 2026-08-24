// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vsr/io/archives/detail/ArchiveClosure.hpp"
#include "vsr/io/archives/detail/ArchivePlan.hpp"

#include <functional>

namespace vsr::animation {
struct AnimationManager;
}

namespace vsr::io::detail {

std::vector<ClosureEntry> closureEntriesForPlan(const ArchivePlan &plan);

bool writeSubtreeAnimations(core::DataNode &animationsNode,
    const animation::AnimationManager &manager,
    const ArchivePlan &plan,
    std::string &errorMessage);

void collectAnimationRefKeys(
    core::DataNode &root, std::vector<ObjectKey> &keys);

bool validateSubtreeAnimations(core::DataNode &root,
    std::vector<FileObjectEntry> &entries,
    core::DataNode &subtree,
    ArchiveValidationResult &result);

bool remapSubtreeAnimationsToTarget(core::DataNode &animations,
    Scene &scene,
    const std::vector<TargetObjectEntry> &targets,
    const std::vector<LayerNodeRef> &createdNodes,
    std::string &errorMessage);

using ObjectIndexRemapper = std::function<size_t(anari::DataType, size_t)>;
using LayerNodeIndexRemapper =
    std::function<size_t(const std::string &, size_t)>;

bool remapSceneAnimations(core::DataNode &animations,
    const ObjectIndexRemapper &remapObject,
    const LayerNodeIndexRemapper &remapLayerNode,
    std::string &errorMessage);

} // namespace vsr::io::detail
