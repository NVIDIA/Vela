// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vsr::animation {
struct AnimationManager;
} // namespace vsr::animation

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::io {

bool serialize_AnimationManagerArchive(
    const animation::AnimationManager &manager, core::DataNode &archive);
bool deserialize_AnimationManagerArchive(
    animation::AnimationManager &manager, core::DataNode &archive);
bool save_AnimationManagerArchive(
    const animation::AnimationManager &manager, const char *filename);
bool load_AnimationManagerArchive(
    animation::AnimationManager &manager, const char *filename);

} // namespace vsr::io
