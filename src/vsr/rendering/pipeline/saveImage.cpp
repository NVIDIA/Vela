// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "vsr/rendering/pipeline/saveImage.h"
// vsr_core
#include "vsr/core/Logging.hpp"
// vsr_rendering
#include "vsr/rendering/pipeline/ImagePipeline.h"
// stb_image
// A private writer keeps other stb callers' process-global settings out of
// this operation. Its orientation stays at the default; row order is local.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace vsr::rendering {

bool saveImage(const ImagePipeline &pipeline, const std::string &filename)
{
  const auto size = pipeline.dimensions();
  const uint32_t *pixels = pipeline.getColorBuffer();
  if (!pixels || size.x == 0 || size.y == 0) {
    vsr::core::logError(
        "[saveImage] nothing rendered, not writing '%s'", filename.c_str());
    return false;
  }

  // Start at the top row and walk backwards through ANARI's bottom-up
  // buffer, without changing writer state or copying the image.
  const uint32_t *topRow = pixels + size_t(size.y - 1) * size.x;
  const int ok = stbi_write_png(
      filename.c_str(), int(size.x), int(size.y), 4, topRow, -int(size.x) * 4);

  if (!ok) {
    vsr::core::logError("[saveImage] failed to write '%s'", filename.c_str());
    return false;
  }
  vsr::core::logStatus("[saveImage] saved '%s'", filename.c_str());
  return true;
}

} // namespace vsr::rendering
