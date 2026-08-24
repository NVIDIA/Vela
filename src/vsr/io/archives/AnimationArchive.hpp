// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <string>

namespace vsr::animation {
struct Animation;
struct AnimationManager;
} // namespace vsr::animation

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::io {

bool serialize_AnimationArchive(
    const animation::Animation &animation, core::DataNode &archive);
bool validate_AnimationArchive(const animation::AnimationManager &manager,
    core::DataNode &archive,
    std::string *message = nullptr);
animation::Animation *deserialize_AnimationArchive(
    animation::AnimationManager &manager, core::DataNode &archive);
bool save_AnimationArchive(
    const animation::Animation &animation, const char *filename);
animation::Animation *load_AnimationArchive(
    animation::AnimationManager &manager, const char *filename);

} // namespace vsr::io
