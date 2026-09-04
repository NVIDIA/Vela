// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vsr::core {
struct DataNode;
} // namespace vsr::core

namespace vsr::rendering {
struct CameraPose;
} // namespace vsr::rendering

namespace vsr::app {

struct Context;

bool serialize_ApplicationDump(const Context &context, core::DataNode &root);
bool deserialize_ApplicationDump(Context &context, core::DataNode &root);

// Reading keeps `pose`'s value for an absent field, fails on a field of the
// wrong type, and assigns `pose` only on success.
void serialize_CameraPose(
    const rendering::CameraPose &pose, core::DataNode &node);
bool deserialize_CameraPose(
    const core::DataNode &node, rendering::CameraPose &pose);

} // namespace vsr::app
