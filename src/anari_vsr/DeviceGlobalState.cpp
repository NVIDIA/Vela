// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DeviceGlobalState.h"

namespace vsr_device {

DeviceGlobalState::DeviceGlobalState(anari::Device d)
    : helium::BaseGlobalDeviceState(d)
{}

bool DeviceGlobalState::usingExternalScene() const
{
  return scene != &localScene;
}

} // namespace vsr_device