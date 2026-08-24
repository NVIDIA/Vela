// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_rendering
#include "vsr/rendering/pipeline/passes/ImagePass.h"

namespace vsr::rendering {

/*
 * ImagePass that copies a caller-owned byte vector into the pipeline's
 * internal color buffer; useful for injecting externally produced frames.
 *
 * Example:
 *   std::vector<uint8_t> externalPixels = receive();
 *   auto *pass = pipeline.emplace_back<CopyToColorBufferPass>();
 *   pass->setExternalBuffer(externalPixels);
 */
struct CopyToColorBufferPass : public ImagePass
{
  CopyToColorBufferPass();
  ~CopyToColorBufferPass() override;

  void setExternalBuffer(std::vector<uint8_t> &buffer);

 private:
  void render(ImageBuffers &b, int stageId) override;

  std::vector<uint8_t> *m_externalBuffer{nullptr};
};

} // namespace vsr::rendering
