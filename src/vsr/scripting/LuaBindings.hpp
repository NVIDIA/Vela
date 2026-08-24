// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace sol {
class state;
}

namespace vsr::scripting {

void registerAllBindings(sol::state &lua);

// Individual binding registration functions
void registerMathBindings(sol::state &lua);
void registerContextBindings(sol::state &lua);
void registerAnimationManagerBindings(sol::state &lua);
void registerObjectBindings(sol::state &lua);
void registerLayerBindings(sol::state &lua);
void registerIOBindings(sol::state &lua);
void registerRenderBindings(sol::state &lua);

} // namespace vsr::scripting
