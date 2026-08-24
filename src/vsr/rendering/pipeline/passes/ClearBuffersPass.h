// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"

namespace vsr::rendering {

/*
 * ImagePass that fills the color buffer with a solid background color each
 * frame; should be the first pass in a pipeline to establish a clean slate.
 *
 * Example:
 *   auto *pass = pipeline.emplace_back<ClearBuffersPass>();
 *   pass->setClearColor({0.1f, 0.1f, 0.1f, 1.f});
 */
struct ClearBuffersPass : public ImagePass
{
  ClearBuffersPass();
  ~ClearBuffersPass() override;

  void setClearColor(const vsr::math::float4 &color);

 private:
  void render(ImageBuffers &b, int stageId) override;

  vsr::math::float4 m_clearColor{0.f, 0.f, 0.f, 1.f};
};

} // namespace vsr::rendering
