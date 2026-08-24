// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/animation/CallbackBinding.hpp"

namespace vsr::animation {

CallbackBinding::CallbackBinding(Callback callback)
    : m_callback(std::move(callback))
{}

void CallbackBinding::update(float t)
{
  if (m_callback)
    m_callback(t);
}

} // namespace vsr::animation
