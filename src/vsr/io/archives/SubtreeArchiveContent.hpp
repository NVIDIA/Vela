// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_core
#include "vsr/core/Any.hpp"
#include "vsr/core/TypeMacros.hpp"
// vsr_io
#include "vsr/io/archives/ArchivePolicies.hpp"
#include "vsr/io/archives/ArchiveValidation.hpp"
// vsr_scene
#include "vsr/scene/Layer.hpp"
// std
#include <string>
#include <string_view>
#include <vector>

namespace vsr::animation {
struct AnimationManager;
} // namespace vsr::animation

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::io {

struct SubtreeArchiveContentDesc
{
  std::string_view fileType;
  std::string_view schema;
  ArchiveObjectPolicy objectPolicy{ArchiveObjectPolicy::All};
};

struct SubtreeArchiveContentOptions
{
  animation::AnimationManager *animationManager{nullptr};
  FileBindingArchivePolicy fileBindings{FileBindingArchivePolicy::Include};
};

struct SubtreeArchiveResult
{
  SubtreeArchiveResult() = default;
  VSR_NOT_COPYABLE(SubtreeArchiveResult)
  VSR_DEFAULT_MOVEABLE(SubtreeArchiveResult)

  bool valid() const;

  scene::LayerNodeRef root;
  // Exact resources created during deserialization. Animation indices remain
  // valid only while the manager is not structurally changed before rollback.
  std::vector<core::Any> createdObjects;
  std::vector<size_t> createdAnimations;

 private:
  bool m_succeeded{false};

  friend SubtreeArchiveResult deserialize_SubtreeArchiveContent(scene::Scene &,
      core::DataNode &,
      scene::LayerNodeRef,
      const SubtreeArchiveContentDesc &,
      std::string *,
      const SubtreeArchiveContentOptions &);
  friend void rollback_SubtreeArchiveContent(
      scene::Scene &, animation::AnimationManager &, SubtreeArchiveResult &);
};

bool serialize_SubtreeArchiveContent(scene::LayerNodeRef root,
    core::DataNode &archive,
    const SubtreeArchiveContentDesc &desc,
    std::string_view displayName = {},
    const SubtreeArchiveContentOptions &options = {});
ArchiveValidationResult validate_SubtreeArchiveContent(
    core::DataNode &archive, const SubtreeArchiveContentDesc &desc);
SubtreeArchiveResult deserialize_SubtreeArchiveContent(scene::Scene &scene,
    core::DataNode &archive,
    scene::LayerNodeRef destinationParent,
    const SubtreeArchiveContentDesc &desc,
    std::string *displayNameOut = nullptr,
    const SubtreeArchiveContentOptions &options = {});
void rollback_SubtreeArchiveContent(scene::Scene &scene,
    animation::AnimationManager &animationManager,
    SubtreeArchiveResult &result);

} // namespace vsr::io
