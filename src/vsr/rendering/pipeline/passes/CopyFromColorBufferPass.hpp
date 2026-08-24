// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// vsr_rendering
#include "vsr/rendering/pipeline/passes/ImagePass.h"

namespace vsr::rendering {

/*
 * ImagePass that copies the pipeline's internal color buffer out to a
 * caller-owned byte vector; useful for extracting rendered pixels for encoding
 * or network transmission.
 *
 * Example:
 *   std::vector<uint8_t> pixels;
 *   auto *pass = pipeline.emplace_back<CopyFromColorBufferPass>();
 *   pass->setExternalBuffer(pixels);
 */
struct CopyFromColorBufferPass : public ImagePass
{
  CopyFromColorBufferPass();
  ~CopyFromColorBufferPass() override;

  void setExternalBuffer(std::vector<uint8_t> &buffer);

 private:
  void render(ImageBuffers &b, int stageId) override;

  std::vector<uint8_t> *m_externalBuffer{nullptr};
};

} // namespace vsr::rendering
