// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
// anari
#include <anari/frontend/anari_enums.h>

namespace vsr::rendering {

struct OutputTransformPass : public ImagePass
{
  OutputTransformPass();
  ~OutputTransformPass() override;
  const char *name() const override;

  void setColorFormat(anari::DataType format);
  void setGamma(float gamma);

 protected:
  void render(ImageBuffers &b, int stageId) override;

 private:
  anari::DataType m_colorFormat{ANARI_UFIXED8_RGBA_SRGB};
  float m_gamma{2.2f};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *OutputTransformPass::name() const
{
  return "Output Transform";
}

} // namespace vsr::rendering
