// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ImagePass.h"
// std
#include <string>

namespace vsr::rendering {

/*
 * ImagePass that writes the current color buffer to an image file; in
 * single-shot mode the pass disables itself after the first successful write.
 *
 * Example:
 *   auto *pass = pipeline.emplace_back<SaveToFilePass>();
 *   pass->setFilename("frame.png");
 *   pass->setSingleShotMode(true);
 */
struct SaveToFilePass : public ImagePass
{
  SaveToFilePass();
  ~SaveToFilePass() override;
  const char *name() const override;

  void setFilename(const std::string &filename);
  const std::string &getFilename() const;

  void setSingleShotMode(bool enabled);

 private:
  void render(ImageBuffers &b, int stageId) override;

  std::string m_filename;
  bool m_singleShot{true};
};

// Inlined definitions ////////////////////////////////////////////////////////

inline const char *SaveToFilePass::name() const
{
  return "Save To File";
}

} // namespace vsr::rendering
