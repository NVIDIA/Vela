// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vsr::animation {
struct AnimationManager;
} // namespace vsr::animation

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::scene {
struct Scene;
} // namespace vsr::scene

namespace vsr::app {

struct Context;

namespace detail {

void serializeLegacyApplicationContext(Context &context, core::DataNode &node);
bool deserializeLegacyApplicationContext(
    Context &context, core::DataNode &node);
bool deserializeLegacySceneState(scene::Scene &scene,
    animation::AnimationManager &animationManager,
    core::DataNode &node);

} // namespace detail

} // namespace vsr::app
