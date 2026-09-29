// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "PickPass.h"

namespace vsr::rendering {

PickPass::PickPass() = default;

PickPass::~PickPass() = default;

void PickPass::setPickOperation(PickOpFunc &&f)
{
  m_op = std::move(f);
}

ImageChannels PickPass::requiredChannels() const
{
  return ImageChannels::DEPTH | ImageChannels::OBJECT_ID;
}

void PickPass::render(ImageBuffers &b, FrameState & /*frame*/)
{
  if (m_op)
    m_op(b);
}

} // namespace vsr::rendering
