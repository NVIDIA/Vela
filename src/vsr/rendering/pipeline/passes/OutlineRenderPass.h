// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"

namespace vsr::rendering {

/*
 * ImagePass that draws a pixel-wide outline around the object whose ID matches
 * a configured value, by scanning the objectId AOV buffer.
 *
 * Example:
 *   auto *pass = pipeline.emplace_back<OutlineRenderPass>();
 *   pass->setOutlineId(selectedObjectId);
 */
struct OutlineRenderPass : public ImagePass
{
  OutlineRenderPass();
  ~OutlineRenderPass() override;
  const char *name() const override;

  void setOutlineId(uint32_t id);

 private:
  void render(ImageBuffers &b, int stageId) override;

  uint32_t m_outlineId{~0u};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *OutlineRenderPass::name() const
{
  return "Outline";
}

} // namespace vsr::rendering
