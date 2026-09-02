// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/animation/FileBinding.hpp"
#include "vsr/animation/AnimationManager.hpp"

namespace vsr::animation {

void FileBinding::reportLoadFailure(int frame, std::string message) const
{
  if (m_manager)
    m_manager->reportLoadFailure(frame, std::move(message));
}

} // namespace vsr::animation
