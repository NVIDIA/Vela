// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
#include "vsr/algorithms/cpu/toneMap.hpp"

namespace vsr::rendering {

using ToneMapOperator = vsr::algorithms::ToneMapOperator;

struct ToneMapPass : public ImagePass
{
  ToneMapPass();
  ~ToneMapPass() override;
  const char *name() const override;

  void setOperator(ToneMapOperator op);
  void setAutoExposureEnabled(bool enabled);
  void setExposure(float exposure);
  void setHDREnabled(bool enabled);

 protected:
  void render(ImageBuffers &b, int stageId) override;

 private:
  ToneMapOperator m_operator{ToneMapOperator::ACES};
  bool m_autoExposureEnabled{false};
  float m_exposure{0.f};
  bool m_hdrEnabled{false};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *ToneMapPass::name() const
{
  return "Tone Map";
}

} // namespace vsr::rendering
